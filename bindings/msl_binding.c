#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#include <Python.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <numpy/arrayobject.h>

#include "msl_preprocess_native.h"
#include "msl_taxonomy_native.h"

#include "../src/alloc.h"
#include "../src/api.h"
#include "../src/anim_table.h"
#include "../src/anim_pose.h"
#include "../src/attack_id_tables.h"
#include "../src/char_params.h"
#include "../src/buttons.h"
#include "../src/common_params.h"
#include "../src/ecb_tables.h"
#include "../src/hitboxes_tables.h"
#include "../src/hurtcaps_tables.h"
#include "../src/hitlist.h"
#include "../src/input_axis.h"
#include "../src/item_article_params.h"
#include "../src/move_tables.h"
#include "../src/motion_state_owners.h"
#include "../src/mpcoll_bounding.h"
#include "../src/mpcoll_end.h"
#include "../src/shield_tilt_table.h"
#include "../src/specialhi_pose.h"
#include "../src/staling.h"
#include "../src/stage_collision.h"
#include "../src/ucf.h"

#pragma pack(push, 1)
typedef struct PyMslControllerPlayer {
  uint8_t A;
  uint8_t B;
  uint8_t X;
  uint8_t Y;
  uint8_t Z;
  uint8_t L;
  uint8_t R;
  uint8_t D_UP;
  float main_stick_x;
  float main_stick_y;
  float c_stick_x;
  float c_stick_y;
  float shoulder;
} PyMslControllerPlayer;

typedef struct PyMslControllerInput {
  PyMslControllerPlayer p[MSL_MAX_PLAYERS];
} PyMslControllerInput;
#pragma pack(pop)

enum {
  PYMSL_ACTION_FORMAT_NONE = 0,
  PYMSL_ACTION_FORMAT_RAW = 1,
  PYMSL_ACTION_FORMAT_CONTROLLER = 2,
};

typedef struct {
  MslBatch* batch;
  PyObject* match_config_obj;
  PyObject* prev_input_obj;
  PyObject* input_obj;
  PyObject* compare_obj;
  PyObject* viewpoint_obj;
  PyObject* gamestate_obj;
  PyObject* terminal_obj;
  PyObject* rollout_action_obj;
  PyObject* rollout_viewpoint_obj;
  PyObject* rollout_gamestate_obj;
  PyObject* rollout_terminal_obj;
  PyObject* rollout_done_obj;
  PyObject* rollout_reset_mask_obj;
  const uint8_t* match_config_bytes;
  size_t match_config_stride;
  const uint8_t* prev_input_bytes;
  size_t prev_input_stride;
  const uint8_t* input_bytes;
  size_t input_stride;
  uint8_t* compare_bytes;
  size_t compare_stride;
  const uint8_t* viewpoint_bytes;
  size_t viewpoint_stride;
  uint8_t* gamestate_bytes;
  size_t gamestate_stride;
  uint8_t* terminal_bytes;
  size_t terminal_stride;
  uint8_t* prev_input_storage;
  uint8_t* input_storage;
  size_t prev_input_storage_stride;
  size_t input_storage_stride;
  int rollout_horizon;
  int rollout_action_format;
  const uint8_t* rollout_action_bytes;
  size_t rollout_action_frame_stride;
  size_t rollout_action_batch_stride;
  const uint8_t* rollout_viewpoint_bytes;
  size_t rollout_viewpoint_stride;
  uint8_t* rollout_gamestate_bytes;
  size_t rollout_gamestate_frame_stride;
  size_t rollout_gamestate_batch_stride;
  uint8_t* rollout_terminal_bytes;
  size_t rollout_terminal_frame_stride;
  size_t rollout_terminal_batch_stride;
  uint8_t* rollout_done_bytes;
  size_t rollout_done_frame_stride;
  size_t rollout_done_batch_stride;
  const uint8_t* rollout_reset_mask_bytes;
  size_t rollout_reset_mask_frame_stride;
  size_t rollout_reset_mask_batch_stride;
} PyMslHandle;

static void pymsl_release_sequence_buffers(PyMslHandle* h) {
  if (h == NULL) {
    return;
  }
  Py_CLEAR(h->rollout_action_obj);
  Py_CLEAR(h->rollout_viewpoint_obj);
  Py_CLEAR(h->rollout_gamestate_obj);
  Py_CLEAR(h->rollout_terminal_obj);
  Py_CLEAR(h->rollout_done_obj);
  Py_CLEAR(h->rollout_reset_mask_obj);
  h->rollout_horizon = 0;
  h->rollout_action_format = PYMSL_ACTION_FORMAT_NONE;
  h->rollout_action_bytes = NULL;
  h->rollout_action_frame_stride = 0u;
  h->rollout_action_batch_stride = 0u;
  h->rollout_viewpoint_bytes = NULL;
  h->rollout_viewpoint_stride = 0u;
  h->rollout_gamestate_bytes = NULL;
  h->rollout_gamestate_frame_stride = 0u;
  h->rollout_gamestate_batch_stride = 0u;
  h->rollout_terminal_bytes = NULL;
  h->rollout_terminal_frame_stride = 0u;
  h->rollout_terminal_batch_stride = 0u;
  h->rollout_done_bytes = NULL;
  h->rollout_done_frame_stride = 0u;
  h->rollout_done_batch_stride = 0u;
  h->rollout_reset_mask_bytes = NULL;
  h->rollout_reset_mask_frame_stride = 0u;
  h->rollout_reset_mask_batch_stride = 0u;
}

static void pymsl_release_bound_buffers(PyMslHandle* h) {
  if (h == NULL) {
    return;
  }
  Py_CLEAR(h->match_config_obj);
  Py_CLEAR(h->prev_input_obj);
  Py_CLEAR(h->input_obj);
  Py_CLEAR(h->compare_obj);
  Py_CLEAR(h->viewpoint_obj);
  Py_CLEAR(h->gamestate_obj);
  Py_CLEAR(h->terminal_obj);
  h->match_config_bytes = NULL;
  h->match_config_stride = 0u;
  h->prev_input_bytes = NULL;
  h->prev_input_stride = 0u;
  h->input_bytes = NULL;
  h->input_stride = 0u;
  h->compare_bytes = NULL;
  h->compare_stride = 0u;
  h->viewpoint_bytes = NULL;
  h->viewpoint_stride = 0u;
  h->gamestate_bytes = NULL;
  h->gamestate_stride = 0u;
  h->terminal_bytes = NULL;
  h->terminal_stride = 0u;
}

static void pymsl_capsule_destructor(PyObject* capsule) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h->prev_input_storage);
  h->prev_input_storage = NULL;
  PyMem_Free(h->input_storage);
  h->input_storage = NULL;
  PyMem_Free(h);
}

PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim,
                                        const char* name) {
  if (!PyObject_TypeCheck(obj, &PyArray_Type)) {
    PyErr_Format(PyExc_TypeError, "%s must be a NumPy array", name);
    return NULL;
  }
  PyArrayObject* arr = (PyArrayObject*)obj;
  if (!PyArray_ISCARRAY(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be contiguous C-order", name);
    return NULL;
  }
  if (PyArray_TYPE(arr) != typenum) {
    PyErr_Format(PyExc_ValueError, "%s has wrong dtype (expected typenum=%d)", name, typenum);
    return NULL;
  }
  if (PyArray_NDIM(arr) < min_ndim) {
    PyErr_Format(PyExc_ValueError, "%s must be at least %dD", name, min_ndim);
    return NULL;
  }
  return arr;
}

PyArrayObject* require_contiguous_array_readonly(PyObject* obj, int typenum, int min_ndim,
                                                 const char* name) {
  if (!PyObject_TypeCheck(obj, &PyArray_Type)) {
    PyErr_Format(PyExc_TypeError, "%s must be a NumPy array", name);
    return NULL;
  }
  PyArrayObject* arr = (PyArrayObject*)obj;
  if (!PyArray_ISCARRAY_RO(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be contiguous C-order", name);
    return NULL;
  }
  if (PyArray_TYPE(arr) != typenum) {
    PyErr_Format(PyExc_ValueError, "%s has wrong dtype (expected typenum=%d)", name, typenum);
    return NULL;
  }
  if (PyArray_NDIM(arr) < min_ndim) {
    PyErr_Format(PyExc_ValueError, "%s must be at least %dD", name, min_ndim);
    return NULL;
  }
  return arr;
}

int require_exact_2d_shape(PyArrayObject* arr, npy_intp rows, npy_intp cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) != cols) {
    PyErr_Format(PyExc_ValueError,
                 "%s must share exact [frames, players] shape: expected [%zd, %zd], got [%zd, %zd]",
                 name, (Py_ssize_t)rows, (Py_ssize_t)cols,
                 PyArray_NDIM(arr) >= 1 ? (Py_ssize_t)PyArray_DIM(arr, 0) : (Py_ssize_t)-1,
                 PyArray_NDIM(arr) >= 2 ? (Py_ssize_t)PyArray_DIM(arr, 1) : (Py_ssize_t)-1);
    return -1;
  }
  return 0;
}

static inline float pymsl_clamp_float(float x, float lo, float hi) {
  if (!isfinite(x)) {
    return lo;
  }
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline int8_t pymsl_libmelee_axis_to_i8(float x) {
  const float clamped = pymsl_clamp_float(x, 0.0f, 1.0f);
  const long raw = lrintf(clamped * 160.0f - 80.0f);
  if (raw < -128) {
    return -128;
  }
  if (raw > 127) {
    return 127;
  }
  return (int8_t)raw;
}

static inline uint8_t pymsl_libmelee_shoulder_to_u8(float x) {
  const float clamped = pymsl_clamp_float(x, 0.0f, 1.0f);
  const long raw = lrintf(clamped * 140.0f);
  if (raw < 0) {
    return 0u;
  }
  if (raw > 255) {
    return 255u;
  }
  return (uint8_t)raw;
}

static inline uint16_t pymsl_controller_buttons(const PyMslControllerPlayer* p) {
  uint16_t buttons = 0u;
  if (p->A) {
    buttons |= (uint16_t)MSL_BUTTON_A;
  }
  if (p->B) {
    buttons |= (uint16_t)MSL_BUTTON_B;
  }
  if (p->X) {
    buttons |= (uint16_t)MSL_BUTTON_X;
  }
  if (p->Y) {
    buttons |= (uint16_t)MSL_BUTTON_Y;
  }
  if (p->Z) {
    buttons |= (uint16_t)MSL_BUTTON_Z;
  }
  if (p->L) {
    buttons |= (uint16_t)MSL_BUTTON_L;
  }
  if (p->R) {
    buttons |= (uint16_t)MSL_BUTTON_R;
  }
  if (p->D_UP) {
    buttons |= (uint16_t)MSL_BUTTON_D_UP;
  }
  return buttons;
}

static void pymsl_controller_to_input(const PyMslControllerInput* src, MslInput* dst) {
  memset(dst, 0, sizeof(*dst));
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    const PyMslControllerPlayer* in = &src->p[p];
    dst->p[p].buttons = pymsl_controller_buttons(in);
    dst->p[p].main_x = pymsl_libmelee_axis_to_i8(in->main_stick_x);
    dst->p[p].main_y = pymsl_libmelee_axis_to_i8(in->main_stick_y);
    dst->p[p].c_x = pymsl_libmelee_axis_to_i8(in->c_stick_x);
    dst->p[p].c_y = pymsl_libmelee_axis_to_i8(in->c_stick_y);
    dst->p[p].l = pymsl_libmelee_shoulder_to_u8(in->shoulder);
    dst->p[p].r = 0u;
  }
}

static inline float f32_from_double(double x) { return (float)x; }
static inline float f32_mul_d(double a, double b) { return (float)(a * b); }
static inline float f32_madd_d(double a, double b, double c) { return (float)(a * b + c); }
static inline float f32_msub_d(double a, double b, double c) { return (float)(a * b - c); }
static inline float f32_fnms_d(double a, double b, double c) { return (float)(-(a * b) + c); }

static inline uint32_t f32_hi_u32(float x) {
  union {
    float f;
    uint32_t u;
  } v;
  v.f = x;
  return v.u;
}

// ─────────────────────────────────────────────────────────────────────────────
// MSL trigf (decomp-first)
//
// Mirrors the instruction-level semantics used in tools/extraction/extract_fighter_anims.py
// (_msl_sinf/_msl_cosf), sourced from:
// - `refs/melee/src/MSL/trigf.c`
// - `refs/melee/build/GALE01/asm/MSL/trigf.s` float constants
// ─────────────────────────────────────────────────────────────────────────────

static const float MSL_EPSILON = 3.45266983e-4f;
static const float MSL_SINCOS_ON_QUADRANT[8] = {0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f};
static const float MSL_SINCOS_POLY[10] = {
    0.0000035287617f, 0.0000003089747f, -0.0003259365f,
    -0.00003657235f,  0.015854323f,     0.0024903931f,
    -0.30842513f,     -0.08074551f,     1.0f,
    0.7853982f,
};
static const float MSL_FOUR_OVER_PI_M1[4] = {
    // Exact float32 literals from `refs/melee/build/GALE01/asm/MSL/trigf.s` `tmp_float`.
    0.25f,
    0.023239374f,
    0.00000017055572f,
    0.00000000001867365f,
};

static inline float msl_sinf(float x) {
  const float xf = x;
  const float z = f32_mul_d(0.63661975, (double)xf);  // trigf.s: 2/pi as float32
  int n = (f32_hi_u32(xf) & 0x80000000u) ? (int)f32_from_double((double)z - 0.5)
                                         : (int)f32_from_double((double)z + 0.5);

  float y = f32_from_double((double)xf - (double)n * 2.0);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[0], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[1], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[2], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[3], xf, y);
  n &= 3;

  if (f32_from_double(fabs((double)y)) < MSL_EPSILON) {
    const int n2 = n << 1;
    const float t = f32_mul_d((double)y, (double)MSL_SINCOS_ON_QUADRANT[n2 + 1]);
    return f32_madd_d((double)MSL_SINCOS_POLY[9], (double)t, (double)MSL_SINCOS_ON_QUADRANT[n2]);
  }

  const double ysq = (double)y * (double)y;
  if (n & 1) {
    const int n2 = n << 1;
    float z2 = f32_madd_d((double)MSL_SINCOS_POLY[0], ysq, (double)MSL_SINCOS_POLY[2]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[4]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[6]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[8]);
    return f32_mul_d((double)z2, (double)MSL_SINCOS_ON_QUADRANT[n2]);
  }

  const int n2 = n << 1;
  float z2 = f32_madd_d((double)MSL_SINCOS_POLY[1], ysq, (double)MSL_SINCOS_POLY[3]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[5]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[7]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[9]);
  z2 = f32_mul_d((double)z2, (double)y);
  return f32_mul_d((double)z2, (double)MSL_SINCOS_ON_QUADRANT[n2 + 1]);
}

static inline float msl_cosf(float x) {
  const float xf = x;
  const float z = f32_mul_d(0.63661975, (double)xf);  // trigf.s: 2/pi as float32
  int n = (f32_hi_u32(xf) & 0x80000000u) ? (int)f32_from_double((double)z - 0.5)
                                         : (int)f32_from_double((double)z + 0.5);

  float y = f32_from_double((double)xf - (double)n * 2.0);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[0], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[1], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[2], xf, y);
  y = f32_madd_d(MSL_FOUR_OVER_PI_M1[3], xf, y);
  n &= 3;

  if (f32_from_double(fabs((double)y)) < MSL_EPSILON) {
    const int n2 = n << 1;
    return f32_fnms_d((double)y, (double)MSL_SINCOS_ON_QUADRANT[n2],
                      (double)MSL_SINCOS_ON_QUADRANT[n2 + 1]);
  }

  const double ysq = (double)y * (double)y;
  if (n & 1) {
    const int n2 = n << 1;
    float z2 = f32_madd_d((double)MSL_SINCOS_POLY[1], ysq, (double)MSL_SINCOS_POLY[3]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[5]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[7]);
    z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[9]);
    z2 = f32_mul_d((double)z2, (double)y);
    z2 = f32_from_double(-(double)z2);
    return f32_mul_d((double)z2, (double)MSL_SINCOS_ON_QUADRANT[n2]);
  }

  const int n2 = n << 1;
  float z2 = f32_madd_d((double)MSL_SINCOS_POLY[0], ysq, (double)MSL_SINCOS_POLY[2]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[4]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[6]);
  z2 = f32_madd_d((double)z2, ysq, (double)MSL_SINCOS_POLY[8]);
  return f32_mul_d((double)z2, (double)MSL_SINCOS_ON_QUADRANT[n2 + 1]);
}

static inline float spl_get_helmite(float fterm, float time, float p0, float p1, float d0,
                                    float d1) {
  // Exact instruction order from `refs/melee/build/GALE01/asm/sysdolphin/baselib/spline.s`
  // `splGetHelmite` (mirrors tools/extraction/extract_fighter_anims.py).
  float f1 = f32_from_double(fterm);
  float f2 = f32_from_double(time);
  float f3 = f32_from_double(p0);
  float f4 = f32_from_double(p1);
  float f5 = f32_from_double(d0);
  float f6 = f32_from_double(d1);

  float f11 = f32_mul_d((double)f2, (double)f2);
  float f10 = f32_mul_d((double)f1, (double)f1);
  float f9 = f32_mul_d((double)f11, (double)f2);
  float f0 = f32_mul_d(3.0, (double)f11);
  f11 = f32_mul_d((double)f11, (double)f1);
  f9 = f32_mul_d((double)f10, (double)f9);
  f10 = f32_mul_d((double)f0, (double)f10);
  f0 = f32_mul_d(2.0, (double)f9);
  f9 = f32_from_double((double)f9 - (double)f11);
  f1 = f32_mul_d((double)f0, (double)f1);
  float f8 = f32_from_double((double)f9 - (double)f11);
  f0 = f32_from_double(-(double)f1);
  f1 = f32_from_double((double)f1 - (double)f10);
  f2 = f32_from_double((double)f2 + (double)f8);
  f0 = f32_from_double((double)f0 + (double)f10);
  f1 = f32_from_double(1.0 + (double)f1);
  f0 = f32_mul_d((double)f4, (double)f0);
  f0 = f32_madd_d((double)f3, (double)f1, (double)f0);
  f0 = f32_madd_d((double)f5, (double)f2, (double)f0);
  f1 = f32_madd_d((double)f6, (double)f9, (double)f0);
  return f32_from_double((double)f1);
}

static inline void mtx_srt(float out[12], const float scl[3], const float rot[3],
                           const float trans[3], const float* parent_scl) {
  // Decomp/ASM source: `refs/melee/build/GALE01/asm/sysdolphin/baselib/mtx.s` `HSD_MtxSRT`.
  const float sx = f32_from_double((double)scl[0]);
  const float sy = f32_from_double((double)scl[1]);
  const float sz = f32_from_double((double)scl[2]);
  const float rx = f32_from_double((double)rot[0]);
  const float ry = f32_from_double((double)rot[1]);
  const float rz = f32_from_double((double)rot[2]);
  const float tx = f32_from_double((double)trans[0]);
  const float ty = f32_from_double((double)trans[1]);
  const float tz = f32_from_double((double)trans[2]);

  const float sin_x = msl_sinf(rx);
  const float cos_x = msl_cosf(rx);
  const float sin_y = msl_sinf(ry);
  const float cos_y = msl_cosf(ry);
  const float sin_z = msl_sinf(rz);
  const float cos_z = msl_cosf(rz);

  float vec1x_2 = sx;
  float vec1y_2 = sy;
  float vec1z_2 = sz;
  float vec1x_1 = sx;
  float vec1y_1 = sy;
  float vec1z_1 = sz;
  float vec1x = sx;
  float vec1y = sy;
  float vec1z = sz;

  if (parent_scl != NULL) {
    const float psx = f32_from_double((double)parent_scl[0]);
    const float psy = f32_from_double((double)parent_scl[1]);
    const float psz = f32_from_double((double)parent_scl[2]);
    // ASM uses a double constant + fdiv + frsp; emulate by rounding the reciprocal to f32.
    const float inv_psx = f32_from_double(1.0 / (double)psx);
    const float inv_psy = f32_from_double(1.0 / (double)psy);
    const float inv_psz = f32_from_double(1.0 / (double)psz);

    vec1y_2 = f32_mul_d((double)vec1y_2, (double)f32_mul_d((double)psy, (double)inv_psx));
    vec1z_2 = f32_mul_d((double)vec1z_2, (double)f32_mul_d((double)psz, (double)inv_psx));
    vec1x_1 = f32_mul_d((double)vec1x_1, (double)f32_mul_d((double)psx, (double)inv_psy));
    vec1z_1 = f32_mul_d((double)vec1z_1, (double)f32_mul_d((double)psz, (double)inv_psy));
    vec1x = f32_mul_d((double)vec1x, (double)f32_mul_d((double)psx, (double)inv_psz));
    vec1y = f32_mul_d((double)vec1y, (double)f32_mul_d((double)psy, (double)inv_psz));
  }

  const float x2_cy = f32_mul_d((double)vec1x_2, (double)cos_y);
  const float x1_cy = f32_mul_d((double)vec1x_1, (double)cos_y);
  const float neg_x = f32_from_double(-(double)vec1x);

  const float m00 = f32_mul_d((double)cos_z, (double)x2_cy);
  const float m10 = f32_mul_d((double)sin_z, (double)x1_cy);
  const float m20 = f32_mul_d((double)neg_x, (double)sin_y);

  const float sinx_siny = f32_mul_d((double)sin_x, (double)sin_y);
  const float cosx_sinz = f32_mul_d((double)cos_x, (double)sin_z);
  const float cosx_cosz = f32_mul_d((double)cos_x, (double)cos_z);

  const float inner01 = f32_msub_d((double)cos_z, (double)sinx_siny, (double)cosx_sinz);
  const float inner11 = f32_madd_d((double)sin_z, (double)sinx_siny, (double)cosx_cosz);

  const float m01 = f32_mul_d((double)vec1y_2, (double)inner01);
  const float m11 = f32_mul_d((double)vec1y_1, (double)inner11);
  const float m21 = f32_mul_d((double)cos_y, (double)f32_mul_d((double)vec1y, (double)sin_x));

  const float cosx_siny = f32_mul_d((double)cos_x, (double)sin_y);
  const float sinx_sinz = f32_mul_d((double)sin_x, (double)sin_z);
  const float sinx_cosz = f32_mul_d((double)sin_x, (double)cos_z);

  const float inner02 = f32_madd_d((double)cos_z, (double)cosx_siny, (double)sinx_sinz);
  const float inner12 = f32_msub_d((double)sin_z, (double)cosx_siny, (double)sinx_cosz);

  const float m02 = f32_mul_d((double)vec1z_2, (double)inner02);
  const float m12 = f32_mul_d((double)vec1z_1, (double)inner12);
  const float m22 = f32_mul_d((double)cos_y, (double)f32_mul_d((double)vec1z, (double)cos_x));

  out[0] = m00;
  out[1] = m01;
  out[2] = m02;
  out[3] = tx;
  out[4] = m10;
  out[5] = m11;
  out[6] = m12;
  out[7] = ty;
  out[8] = m20;
  out[9] = m21;
  out[10] = m22;
  out[11] = tz;
}

static inline void mtx_concat(float out[12], const float a[12], const float b[12]) {
  // Decomp-first: emulate Dolphin SDK `PSMTXConcat` ordering.
  const float a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
  const float a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
  const float a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];

  const float b00 = b[0], b01 = b[1], b02 = b[2], b03 = b[3];
  const float b10 = b[4], b11 = b[5], b12 = b[6], b13 = b[7];
  const float b20 = b[8], b21 = b[9], b22 = b[10], b23 = b[11];

  // Row 0
  float o00 = f32_madd_d(
      (double)b20, (double)a02,
      (double)f32_madd_d((double)b10, (double)a01, (double)f32_mul_d((double)b00, (double)a00)));
  float o01 = f32_madd_d(
      (double)b21, (double)a02,
      (double)f32_madd_d((double)b11, (double)a01, (double)f32_mul_d((double)b01, (double)a00)));
  float o02 = f32_madd_d(
      (double)b22, (double)a02,
      (double)f32_madd_d((double)b12, (double)a01, (double)f32_mul_d((double)b02, (double)a00)));
  float o03 = f32_madd_d(
      (double)b23, (double)a02,
      (double)f32_madd_d((double)b13, (double)a01, (double)f32_mul_d((double)b03, (double)a00)));
  o03 = f32_madd_d(1.0, (double)a03, (double)o03);

  // Row 1
  float o10 = f32_madd_d(
      (double)b20, (double)a12,
      (double)f32_madd_d((double)b10, (double)a11, (double)f32_mul_d((double)b00, (double)a10)));
  float o11 = f32_madd_d(
      (double)b21, (double)a12,
      (double)f32_madd_d((double)b11, (double)a11, (double)f32_mul_d((double)b01, (double)a10)));
  float o12 = f32_madd_d(
      (double)b22, (double)a12,
      (double)f32_madd_d((double)b12, (double)a11, (double)f32_mul_d((double)b02, (double)a10)));
  float o13 = f32_madd_d(
      (double)b23, (double)a12,
      (double)f32_madd_d((double)b13, (double)a11, (double)f32_mul_d((double)b03, (double)a10)));
  o13 = f32_madd_d(1.0, (double)a13, (double)o13);

  // Row 2
  float o20 = f32_madd_d(
      (double)b20, (double)a22,
      (double)f32_madd_d((double)b10, (double)a21, (double)f32_mul_d((double)b00, (double)a20)));
  float o21 = f32_madd_d(
      (double)b21, (double)a22,
      (double)f32_madd_d((double)b11, (double)a21, (double)f32_mul_d((double)b01, (double)a20)));
  float o22 = f32_madd_d(
      (double)b22, (double)a22,
      (double)f32_madd_d((double)b12, (double)a21, (double)f32_mul_d((double)b02, (double)a20)));
  float o23 = f32_madd_d(
      (double)b23, (double)a22,
      (double)f32_madd_d((double)b13, (double)a21, (double)f32_mul_d((double)b03, (double)a20)));
  o23 = f32_madd_d(1.0, (double)a23, (double)o23);

  out[0] = o00;
  out[1] = o01;
  out[2] = o02;
  out[3] = o03;
  out[4] = o10;
  out[5] = o11;
  out[6] = o12;
  out[7] = o13;
  out[8] = o20;
  out[9] = o21;
  out[10] = o22;
  out[11] = o23;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal FObj interpreter (sysdolphin/baselib/fobj.c port, decomp-first)
// Matches tools/extraction/extract_fighter_anims.py semantics.
// ─────────────────────────────────────────────────────────────────────────────

enum {
  HSD_A_OP_CON = 1,
  HSD_A_OP_LIN = 2,
  HSD_A_OP_SPL0 = 3,
  HSD_A_OP_SPL = 4,
  HSD_A_OP_SLP = 5,
  HSD_A_OP_KEY = 6,
};

enum {
  HSD_A_FRAC_FLOAT = 0 << 5,
  HSD_A_FRAC_S16 = 1 << 5,
  HSD_A_FRAC_U16 = 2 << 5,
  HSD_A_FRAC_S8 = 3 << 5,
  HSD_A_FRAC_U8 = 4 << 5,
};

enum {
  FOBJ_LOAD_DATA0 = 1,
  FOBJ_LOAD_DATA = 2,
  FOBJ_LOAD_WAIT = 3,
};

typedef struct {
  const uint8_t* ad;
  int length;
  int startframe;
  uint8_t obj_type;
  uint8_t frac_value;
  uint8_t frac_slope;

  int state;
  int op;
  int op_intrp;
  float time;
  int nb_pack;
  int fterm;
  float p0;
  float p1;
  float d0;
  float d1;
  bool flags_20;
  bool flags_40;
  bool flags_80;
  int pos;
} FObj;

static inline float fobj_parse_float(const uint8_t* ad, int len, int* pos, uint8_t frac) {
  const int p = *pos;
  if ((frac & 0xE0) == HSD_A_FRAC_FLOAT) {
    if (p + 4 > len) {
      return 0.0f;
    }
    uint32_t d = (uint32_t)ad[p] | ((uint32_t)ad[p + 1] << 8) | ((uint32_t)ad[p + 2] << 16) |
                 ((uint32_t)ad[p + 3] << 24);
    union {
      uint32_t u;
      float f;
    } v;
    v.u = d;
    *pos = p + 4;
    return v.f;
  }

  const int denom = 1 << (frac & 0x1F);
  const uint8_t kind = frac & 0xE0;
  if (kind == HSD_A_FRAC_S8) {
    if (p + 1 > len) {
      return 0.0f;
    }
    const int8_t numer = (int8_t)ad[p];
    *pos = p + 1;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_U8) {
    if (p + 1 > len) {
      return 0.0f;
    }
    const uint8_t numer = ad[p];
    *pos = p + 1;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_S16) {
    if (p + 2 > len) {
      return 0.0f;
    }
    const int16_t numer = (int16_t)((uint16_t)ad[p] | ((uint16_t)ad[p + 1] << 8));
    *pos = p + 2;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_U16) {
    if (p + 2 > len) {
      return 0.0f;
    }
    const uint16_t numer = (uint16_t)ad[p] | ((uint16_t)ad[p + 1] << 8);
    *pos = p + 2;
    return f32_from_double((double)numer / (double)denom);
  }
  return 0.0f;
}

static inline uint8_t fobj_parse_opcode(const uint8_t* ad, int pos) {
  return (uint8_t)(ad[pos] & 0xF);
}

static inline int fobj_parse_pack_info(const uint8_t* ad, int len, int* pos) {
  (void)len;
  int p = *pos;
  int d = (int)ad[p++];
  int nb_pack = ((d >> 4) & 7) + 1;
  int shift = 3;
  if ((d & 0x80) == 0) {
    *pos = p;
    return nb_pack;
  }
  while (true) {
    d = (int)ad[p++];
    nb_pack += (d & 0x7F) << shift;
    shift += 7;
    if ((d & 0x80) == 0) {
      break;
    }
  }
  *pos = p;
  return nb_pack;
}

static inline int fobj_parse_wait(const uint8_t* ad, int len, int* pos) {
  (void)len;
  int p = *pos;
  int wait = 0;
  int shift = 0;
  while (true) {
    const int d = (int)ad[p++];
    wait |= (d & 0x7F) << shift;
    shift += 7;
    if ((d & 0x80) == 0) {
      break;
    }
  }
  *pos = p;
  return wait;
}

static inline void fobj_req_anim(FObj* fo, float frame) {
  fo->pos = 0;
  fo->time = f32_from_double((double)fo->startframe + (double)frame);
  fo->op = 0;
  fo->op_intrp = 0;
  fo->flags_40 = false;
  fo->flags_80 = false;
  fo->flags_20 = false;
  fo->nb_pack = 0;
  fo->fterm = 0;
  fo->p0 = 0.0f;
  fo->p1 = 0.0f;
  fo->d0 = 0.0f;
  fo->d1 = 0.0f;
  fo->state = FOBJ_LOAD_DATA0;
}

static inline void fobj_launch_key_data(FObj* fo) {
  if (fo->flags_40) {
    fo->op_intrp = fo->op;
    fo->flags_40 = false;
    fo->flags_80 = true;
    fo->p0 = fo->p1;
  }
}

static inline int fobj_anim_con(FObj* fo) {
  fo->p0 = fo->p1;
  fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value);
  if (fo->op_intrp != HSD_A_OP_SLP) {
    fo->d0 = fo->d1;
    fo->d1 = 0.0f;
  }
  return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
}
static inline int fobj_anim_lin(FObj* fo) {
  fo->p0 = fo->p1;
  fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value);
  if (fo->op_intrp != HSD_A_OP_SLP) {
    fo->d0 = fo->d1;
    fo->d1 = 0.0f;
  }
  return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
}
static inline int fobj_anim_spl0(FObj* fo) {
  fo->p0 = fo->p1;
  fo->d0 = fo->d1;
  fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value);
  fo->d1 = 0.0f;
  return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
}
static inline int fobj_anim_spl(FObj* fo) {
  fo->p0 = fo->p1;
  fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value);
  fo->d0 = fo->d1;
  fo->d1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_slope);
  return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
}
static inline int fobj_anim_slp(FObj* fo) {
  fo->d0 = fo->d1;
  fo->d1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_slope);
  return fo->state;
}
static inline int fobj_anim_key(FObj* fo) {
  fobj_launch_key_data(fo);
  fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value);
  fo->flags_40 = true;
  return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
}

static inline int fobj_load_data(FObj* fo) {
  if (fo->pos >= fo->length) {
    return 6;
  }
  fo->op_intrp = fo->op;
  if (fo->nb_pack == 0) {
    fo->op = (int)fobj_parse_opcode(fo->ad, fo->pos);
    fo->nb_pack = fobj_parse_pack_info(fo->ad, fo->length, &fo->pos);
  }
  fo->nb_pack -= 1;

  if (fo->op == HSD_A_OP_CON) {
    fo->state = fobj_anim_con(fo);
  } else if (fo->op == HSD_A_OP_LIN) {
    fo->state = fobj_anim_lin(fo);
  } else if (fo->op == HSD_A_OP_SPL0) {
    fo->state = fobj_anim_spl0(fo);
  } else if (fo->op == HSD_A_OP_SPL) {
    fo->state = fobj_anim_spl(fo);
  } else if (fo->op == HSD_A_OP_SLP) {
    fo->state = fobj_anim_slp(fo);
  } else if (fo->op == HSD_A_OP_KEY) {
    fo->state = fobj_anim_key(fo);
  } else {
    fo->state = 0;
  }
  return fo->state;
}

static inline int fobj_load_wait(FObj* fo) {
  if (fo->pos >= fo->length) {
    fo->state = 6;
    return fo->state;
  }
  fo->fterm = fobj_parse_wait(fo->ad, fo->length, &fo->pos);
  fo->flags_20 = true;
  fo->state = FOBJ_LOAD_DATA;
  return fo->state;
}

static inline bool fobj_update_anim(FObj* fo, float* out_value) {
  if (fo->op_intrp == HSD_A_OP_KEY) {
    if (fo->flags_80) {
      fo->flags_80 = false;
      *out_value = fo->p0;
      return true;
    }
    return false;
  }

  if (fo->op_intrp == HSD_A_OP_CON) {
    *out_value = (fo->time >= (float)fo->fterm) ? fo->p1 : fo->p0;
    return true;
  }

  if (fo->op_intrp == HSD_A_OP_LIN) {
    if (fo->flags_20) {
      fo->flags_20 = false;
      if (fo->fterm != 0) {
        fo->d0 = f32_from_double(((double)fo->p1 - (double)fo->p0) / (double)fo->fterm);
      } else {
        fo->d0 = 0.0f;
        fo->p0 = fo->p1;
      }
    }
    *out_value = f32_madd_d((double)fo->d0, (double)fo->time, (double)fo->p0);
    return true;
  }

  if (fo->op_intrp == HSD_A_OP_SPL0 || fo->op_intrp == HSD_A_OP_SPL ||
      fo->op_intrp == HSD_A_OP_SLP) {
    if (fo->fterm == 0) {
      *out_value = fo->p1;
      return true;
    }
    const float inv = f32_from_double(1.0 / (double)fo->fterm);
    *out_value = spl_get_helmite(inv, fo->time, fo->p0, fo->p1, fo->d0, fo->d1);
    return true;
  }

  return false;
}

static inline bool fobj_interpret(FObj* fo, float rate, float* out_value) {
  bool any = false;
  float last = 0.0f;

  if (fo->state == 0) {
    return false;
  }
  fo->time = f32_from_double((double)fo->time + (double)rate);
  if (fo->time < 0.0f) {
    return false;
  }

  float fterm = 0.0f;
  int iters = 0;
  while (true) {
    iters += 1;
    if (iters > 100000) {
      if (any) {
        *out_value = last;
      }
      return any;
    }
    const int st = fo->state;
    if (st == 6) {
      fo->time = f32_from_double((double)fo->time + (double)fterm);
      fobj_launch_key_data(fo);
      float v = 0.0f;
      if (fobj_update_anim(fo, &v)) {
        last = v;
        any = true;
      }
      *out_value = last;
      return any;
    }
    if (st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA) {
      (void)fobj_load_data(fo);
      continue;
    }
    if (st == FOBJ_LOAD_WAIT) {
      if (fo->flags_80) {
        float v = 0.0f;
        if (fobj_update_anim(fo, &v)) {
          last = v;
          any = true;
        }
      }
      (void)fobj_load_wait(fo);
      continue;
    }
    if (st == 4) {
      if ((float)fo->fterm <= fo->time) {
        fterm = (float)fo->fterm;
        fo->time = f32_from_double((double)fo->time - (double)fo->fterm);
        fo->state = FOBJ_LOAD_WAIT;
        continue;
      }
      float v = 0.0f;
      if (fobj_update_anim(fo, &v)) {
        last = v;
        any = true;
      }
      fo->state = 5;
      *out_value = last;
      return any;
    }
    if (st == 5) {
      fo->state = 4;
      continue;
    }
    *out_value = last;
    return any;
  }
}

static inline bool fobj_stop_anim(FObj* fo, float rate, float* out_value) {
  // HSD_FObjStopAnim flushes only KEY tracks before clearing state.
  // refs/melee/src/sysdolphin/baselib/fobj.c::{FObj_FlushKeyData,HSD_FObjStopAnim}
  bool any = false;
  if (fo != NULL && fo->op_intrp == HSD_A_OP_KEY) {
    any = fobj_interpret(fo, rate, out_value);
  }
  if (fo != NULL) {
    fo->state = 0;
  }
  return any;
}

static inline bool fobj_stopped_terminal_value(const FObj* fo, float* out_value) {
  // Native probes on stopped non-loop fighter AObjs show JObj local SRT carrying the final loaded
  // FObj p1 value when the AObj is at end_frame with AOBJ_NO_ANIM set. The decomp owner is the
  // same HSD AObj/FObj end path used by ftAnim_8006EBA4.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
  if (fo == NULL || out_value == NULL) {
    return false;
  }
  if (fo->state == FOBJ_LOAD_DATA && fo->pos >= fo->length) {
    *out_value = fo->p1;
    return true;
  }
  return false;
}

static PyObject* msl_init(PyObject* self, PyObject* args, PyObject* kwargs) {
  (void)self;
  static const char* kwlist[] = {
      "batch_size", "num_players", "ucf_enabled", "ucf_cardinals_1_0_enabled", NULL,
  };
  int batch_size = 0;
  int num_players = 0;
  int ucf_enabled = -1;
  int ucf_cardinals_1_0_enabled = -1;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|ii", (char**)kwlist, &batch_size, &num_players,
                                   &ucf_enabled, &ucf_cardinals_1_0_enabled)) {
    return NULL;
  }

  MslBatch* batch = msl_batch_create(batch_size, num_players);
  if (batch == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "msl_batch_create failed; see stderr for data loading details");
    return NULL;
  }
  if (ucf_enabled != -1) {
    const int err = msl_batch_set_ucf_enabled(batch, ucf_enabled != 0);
    if (err != 0) {
      msl_batch_destroy(batch);
      PyErr_Format(PyExc_RuntimeError, "msl_batch_set_ucf_enabled failed: %d", err);
      return NULL;
    }
  }
  if (ucf_cardinals_1_0_enabled != -1) {
    const int err = msl_batch_set_ucf_cardinals_1_0_enabled(batch, ucf_cardinals_1_0_enabled != 0);
    if (err != 0) {
      msl_batch_destroy(batch);
      PyErr_Format(PyExc_RuntimeError, "msl_batch_set_ucf_cardinals_1_0_enabled failed: %d", err);
      return NULL;
    }
  }

  PyMslHandle* h = (PyMslHandle*)PyMem_Calloc(1u, sizeof(PyMslHandle));
  if (h == NULL) {
    msl_batch_destroy(batch);
    PyErr_NoMemory();
    return NULL;
  }
  h->batch = batch;
  h->prev_input_storage_stride = sizeof(MslInput);
  h->prev_input_storage = (uint8_t*)PyMem_Calloc((size_t)batch_size, h->prev_input_storage_stride);
  h->input_storage_stride = sizeof(MslInput);
  h->input_storage = (uint8_t*)PyMem_Calloc((size_t)batch_size, h->input_storage_stride);
  if (h->prev_input_storage == NULL || h->input_storage == NULL) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
    PyMem_Free(h->prev_input_storage);
    PyMem_Free(h->input_storage);
    PyMem_Free(h);
    PyErr_NoMemory();
    return NULL;
  }

  PyObject* capsule = PyCapsule_New(h, "msl.Handle", pymsl_capsule_destructor);
  if (capsule == NULL) {
    // PyCapsule_New sets an exception on failure. We must clean up manually here because the capsule
    // was never created (calling the capsule destructor with NULL would be a bug).
    msl_batch_destroy(h->batch);
    h->batch = NULL;
    PyMem_Free(h->prev_input_storage);
    h->prev_input_storage = NULL;
    PyMem_Free(h->input_storage);
    h->input_storage = NULL;
    PyMem_Free(h);
    return NULL;
  }
  return capsule;
}

static PyMslHandle* unpack_handle(PyObject* handle_obj) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(handle_obj, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid msl handle");
    return NULL;
  }
  return h;
}

static PyObject* msl_reseed_seed(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &seed_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_reseed_seed(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_reseed_seed failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_reseed_seed_rollout(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &seed_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_reseed_seed_rollout(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_reseed_seed_rollout failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_init_match(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* config_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &config_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* config = require_contiguous_array(config_obj, NPY_UINT8, 2, "match_config");
  if (config == NULL) {
    return NULL;
  }
  if (PyArray_DIM(config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(config, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "match_config has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(config, 0), batch_size);
    return NULL;
  }

  const uint8_t* config_bytes = (const uint8_t*)PyArray_DATA(config);
  const size_t stride = (size_t)PyArray_STRIDE(config, 0);

  const int err = msl_batch_init_match(h->batch, config_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);

  Py_RETURN_NONE;
}

static PyObject* msl_init_match_masked(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* config_obj = NULL;
  PyObject* mask_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &config_obj, &mask_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* config = require_contiguous_array(config_obj, NPY_UINT8, 2, "match_config");
  if (config == NULL) {
    return NULL;
  }
  if (PyArray_DIM(config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }
  PyArrayObject* mask = require_contiguous_array(mask_obj, NPY_UINT8, 1, "mask");
  if (mask == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(config, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "match_config has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(config, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(mask, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "mask has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(mask, 0), batch_size);
    return NULL;
  }

  const uint8_t* config_bytes = (const uint8_t*)PyArray_DATA(config);
  const size_t config_stride = (size_t)PyArray_STRIDE(config, 0);
  const uint8_t* mask_bytes = (const uint8_t*)PyArray_DATA(mask);
  const size_t mask_stride = (size_t)PyArray_STRIDE(mask, 0);

  const int err =
      msl_batch_init_match_masked(h->batch, config_bytes, config_stride, mask_bytes, mask_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match_masked failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_copy_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* dst_handle_obj = NULL;
  PyObject* src_handle_obj = NULL;
  PyObject* dst_lanes_obj = NULL;
  PyObject* src_lanes_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOO", &dst_handle_obj, &src_handle_obj, &dst_lanes_obj,
                        &src_lanes_obj)) {
    return NULL;
  }
  PyMslHandle* dst = unpack_handle(dst_handle_obj);
  if (dst == NULL) {
    return NULL;
  }
  PyMslHandle* src = unpack_handle(src_handle_obj);
  if (src == NULL) {
    return NULL;
  }
  PyArrayObject* dst_lanes = require_contiguous_array(dst_lanes_obj, NPY_INT32, 1, "dst_lanes");
  if (dst_lanes == NULL) {
    return NULL;
  }
  PyArrayObject* src_lanes = require_contiguous_array(src_lanes_obj, NPY_INT32, 1, "src_lanes");
  if (src_lanes == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(dst_lanes) != 1 || PyArray_NDIM(src_lanes) != 1) {
    PyErr_SetString(PyExc_ValueError, "lane arrays must be 1D");
    return NULL;
  }
  if (PyArray_DIM(dst_lanes, 0) != PyArray_DIM(src_lanes, 0)) {
    PyErr_SetString(PyExc_ValueError, "dst_lanes and src_lanes length mismatch");
    return NULL;
  }
  if (PyArray_DIM(dst_lanes, 0) > (npy_intp)INT32_MAX) {
    PyErr_SetString(PyExc_ValueError, "lane array too large");
    return NULL;
  }

  const int32_t* dst_lane_data = (const int32_t*)PyArray_DATA(dst_lanes);
  const int32_t* src_lane_data = (const int32_t*)PyArray_DATA(src_lanes);
  const int32_t count = (int32_t)PyArray_DIM(dst_lanes, 0);
  const int err = msl_batch_copy_lanes(dst->batch, src->batch, dst_lane_data, src_lane_data, count);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_copy_lanes failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_copy_colldata_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int src_player_index = 0;
  int dst_player_index = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &src_player_index,
                        &dst_player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err =
      msl_batch_debug_copy_colldata(h->batch, batch_index, src_player_index, dst_player_index);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_copy_colldata failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_step_input(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &prev_input_obj, &input_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL) {
    return NULL;
  }

  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  const uint8_t* prev_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  const uint8_t* in_bytes = (const uint8_t*)PyArray_DATA(input);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_input, 0);
  const size_t in_stride = (size_t)PyArray_STRIDE(input, 0);

  const int err = msl_batch_step_input(h->batch, prev_bytes, prev_stride, in_bytes, in_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_step_input_pre_combat(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &prev_input_obj, &input_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL) {
    return NULL;
  }

  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  const uint8_t* prev_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  const uint8_t* in_bytes = (const uint8_t*)PyArray_DATA(input);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_input, 0);
  const size_t in_stride = (size_t)PyArray_STRIDE(input, 0);

  const int err =
      msl_batch_debug_step_input_pre_combat(h->batch, prev_bytes, prev_stride, in_bytes, in_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_step_input_pre_combat failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_knockdown_update_pre_physics(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  const int err = msl_batch_debug_knockdown_update_pre_physics(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_knockdown_update_pre_physics failed: %d",
                 err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_coll_env_flags_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int flags = 0;
  if (!PyArg_ParseTuple(args, "OiiI", &handle_obj, &batch_index, &player_index, &flags)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_coll_env_flags(h->batch, batch_index, player_index, flags);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_coll_env_flags failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_mpcoll_joint_filters_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int joint_id_skip = -1;
  int joint_id_only = -1;
  if (!PyArg_ParseTuple(args, "Oiiii", &handle_obj, &batch_index, &player_index, &joint_id_skip,
                        &joint_id_only)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_mpcoll_joint_filters(h->batch, batch_index, player_index,
                                                           joint_id_skip, joint_id_only);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_mpcoll_joint_filters failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_escapeair_floor_producer_runtime_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int authority = 0;
  unsigned int desired_owner = 0;
  if (!PyArg_ParseTuple(args, "OiiII", &handle_obj, &batch_index, &player_index, &authority,
                        &desired_owner)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_escapeair_floor_producer_runtime(
      h->batch, batch_index, player_index, (uint8_t)authority, (uint8_t)desired_owner);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError,
                 "msl_batch_debug_set_escapeair_floor_producer_runtime failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_floor_sweep_prev_runtime_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  unsigned int authority = 0;
  if (!PyArg_ParseTuple(args, "OiiffI", &handle_obj, &batch_index, &player_index, &pos_x, &pos_y,
                        &authority)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_floor_sweep_prev_runtime(h->batch, batch_index, player_index,
                                                               pos_x, pos_y, (uint8_t)authority);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_floor_sweep_prev_runtime failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_wall_ceil_prev_runtime_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  unsigned int authority = 0;
  if (!PyArg_ParseTuple(args, "OiiffI", &handle_obj, &batch_index, &player_index, &pos_x, &pos_y,
                        &authority)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_wall_ceil_prev_runtime(h->batch, batch_index, player_index,
                                                             pos_x, pos_y, (uint8_t)authority);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_wall_ceil_prev_runtime failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_player_root_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  int facing = 0;
  if (!PyArg_ParseTuple(args, "Oiiffi", &handle_obj, &batch_index, &player_index, &pos_x, &pos_y,
                        &facing)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_player_root(h->batch, batch_index, player_index, pos_x, pos_y,
                                                  (uint8_t)(facing ? 1 : 0));
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_player_root failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_ceiling_contact_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  float contact_y = 0.0f;
  if (!PyArg_ParseTuple(args, "Oiif", &handle_obj, &batch_index, &player_index, &contact_y)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err =
      msl_batch_debug_set_ceiling_contact(h->batch, batch_index, player_index, contact_y);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_ceiling_contact failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_run_knockdown_post_collision_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_run_knockdown_post_collision(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_run_knockdown_post_collision failed: %d",
                 err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_run_locomotion_post_collision_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_run_locomotion_post_collision(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_run_locomotion_post_collision failed: %d",
                 err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_refresh_combat_geometry(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  const int err = msl_batch_debug_refresh_combat_geometry(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_refresh_combat_geometry failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_write_compare(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslCompare");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_compare(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_compare failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static int require_batch_rows(PyArrayObject* arr, int batch_size, const char* name) {
  if (PyArray_DIM(arr, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "%s has too few rows: got %zd, need %d", name,
                 (Py_ssize_t)PyArray_DIM(arr, 0), batch_size);
    return -1;
  }
  return 0;
}

static PyObject* msl_bind_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* match_config_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* compare_obj = NULL;
  PyObject* viewpoint_obj = Py_None;
  PyObject* gamestate_obj = Py_None;
  PyObject* terminal_obj = Py_None;
  if (!PyArg_ParseTuple(args, "OOOOO|OOO", &handle_obj, &match_config_obj, &prev_input_obj,
                        &input_obj, &compare_obj, &viewpoint_obj, &gamestate_obj, &terminal_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_sequence_buffers(h);
  const int batch_size = msl_batch_batch_size(h->batch);

  PyArrayObject* match_config =
      require_contiguous_array(match_config_obj, NPY_UINT8, 2, "match_config");
  if (match_config == NULL || require_batch_rows(match_config, batch_size, "match_config") != 0) {
    return NULL;
  }
  if (PyArray_DIM(match_config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }

  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL || require_batch_rows(prev_input, batch_size, "prev_input") != 0) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL || require_batch_rows(input, batch_size, "input") != 0) {
    return NULL;
  }
  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  PyArrayObject* compare = require_contiguous_array(compare_obj, NPY_UINT8, 2, "compare");
  if (compare == NULL || require_batch_rows(compare, batch_size, "compare") != 0) {
    return NULL;
  }
  if (PyArray_DIM(compare, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "compare second dim too small for MslCompare");
    return NULL;
  }

  PyArrayObject* viewpoint = NULL;
  if (viewpoint_obj != Py_None) {
    viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
    if (viewpoint == NULL || require_batch_rows(viewpoint, batch_size, "viewpoint") != 0) {
      return NULL;
    }
  }

  PyArrayObject* gamestate = NULL;
  if (gamestate_obj != Py_None) {
    gamestate = require_contiguous_array(gamestate_obj, NPY_UINT8, 2, "gamestate");
    if (gamestate == NULL || require_batch_rows(gamestate, batch_size, "gamestate") != 0) {
      return NULL;
    }
    if (PyArray_DIM(gamestate, 1) < (npy_intp)sizeof(MeleeGamestate)) {
      PyErr_SetString(PyExc_ValueError, "gamestate second dim too small for MeleeGamestate");
      return NULL;
    }
  }

  PyArrayObject* terminal = NULL;
  if (terminal_obj != Py_None) {
    terminal = require_contiguous_array(terminal_obj, NPY_UINT8, 2, "terminal");
    if (terminal == NULL || require_batch_rows(terminal, batch_size, "terminal") != 0) {
      return NULL;
    }
    if (PyArray_DIM(terminal, 1) < (npy_intp)sizeof(MslTerminal)) {
      PyErr_SetString(PyExc_ValueError, "terminal second dim too small for MslTerminal");
      return NULL;
    }
  }

  pymsl_release_bound_buffers(h);

  Py_INCREF(match_config_obj);
  h->match_config_obj = match_config_obj;
  h->match_config_bytes = (const uint8_t*)PyArray_DATA(match_config);
  h->match_config_stride = (size_t)PyArray_STRIDE(match_config, 0);

  Py_INCREF(prev_input_obj);
  h->prev_input_obj = prev_input_obj;
  h->prev_input_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  h->prev_input_stride = (size_t)PyArray_STRIDE(prev_input, 0);

  Py_INCREF(input_obj);
  h->input_obj = input_obj;
  h->input_bytes = (const uint8_t*)PyArray_DATA(input);
  h->input_stride = (size_t)PyArray_STRIDE(input, 0);

  Py_INCREF(compare_obj);
  h->compare_obj = compare_obj;
  h->compare_bytes = (uint8_t*)PyArray_DATA(compare);
  h->compare_stride = (size_t)PyArray_STRIDE(compare, 0);

  if (viewpoint_obj != Py_None) {
    Py_INCREF(viewpoint_obj);
    h->viewpoint_obj = viewpoint_obj;
    h->viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
    h->viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);
  }
  if (gamestate_obj != Py_None) {
    Py_INCREF(gamestate_obj);
    h->gamestate_obj = gamestate_obj;
    h->gamestate_bytes = (uint8_t*)PyArray_DATA(gamestate);
    h->gamestate_stride = (size_t)PyArray_STRIDE(gamestate, 0);
  }
  if (terminal_obj != Py_None) {
    Py_INCREF(terminal_obj);
    h->terminal_obj = terminal_obj;
    h->terminal_bytes = (uint8_t*)PyArray_DATA(terminal);
    h->terminal_stride = (size_t)PyArray_STRIDE(terminal, 0);
  }

  Py_RETURN_NONE;
}

static PyObject* msl_unbind_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  Py_RETURN_NONE;
}

static int require_sequence_rows(PyArrayObject* arr, int length, int batch_size, const char* name) {
  if (PyArray_NDIM(arr) != 3 || PyArray_DIM(arr, 0) < (npy_intp)length ||
      PyArray_DIM(arr, 1) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError,
                 "%s must have shape [length, batch, bytes] with at least [%d, %d, ...]", name,
                 length, batch_size);
    return -1;
  }
  return 0;
}

static PyObject* msl_bind_sequence_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* match_config_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* compare_obj = NULL;
  PyObject* viewpoint_obj = NULL;
  PyObject* gamestate_obj = NULL;
  PyObject* terminal_obj = NULL;
  PyObject* done_obj = NULL;
  PyObject* reset_mask_obj = NULL;
  const char* action_format = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOs", &handle_obj, &match_config_obj, &action_obj,
                        &compare_obj, &viewpoint_obj, &gamestate_obj, &terminal_obj, &done_obj,
                        &reset_mask_obj, &action_format)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);

  PyArrayObject* match_config =
      require_contiguous_array(match_config_obj, NPY_UINT8, 2, "match_config");
  if (match_config == NULL || require_batch_rows(match_config, batch_size, "match_config") != 0) {
    return NULL;
  }
  if (PyArray_DIM(match_config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }

  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT8, 3, "action");
  if (action == NULL) {
    return NULL;
  }
  const int horizon = (int)PyArray_DIM(action, 0);
  if (horizon <= 0 || require_sequence_rows(action, horizon, batch_size, "action") != 0) {
    return NULL;
  }
  int action_format_id = PYMSL_ACTION_FORMAT_NONE;
  size_t action_row_size = 0u;
  if (strcmp(action_format, "raw") == 0) {
    action_format_id = PYMSL_ACTION_FORMAT_RAW;
    action_row_size = sizeof(MslInput);
  } else if (strcmp(action_format, "controller") == 0) {
    action_format_id = PYMSL_ACTION_FORMAT_CONTROLLER;
    action_row_size = sizeof(PyMslControllerInput);
  } else {
    PyErr_SetString(PyExc_ValueError, "action_format must be 'raw' or 'controller'");
    return NULL;
  }
  if (PyArray_DIM(action, 2) < (npy_intp)action_row_size) {
    PyErr_Format(PyExc_ValueError, "action third dim too small for %s action", action_format);
    return NULL;
  }

  PyArrayObject* compare = require_contiguous_array(compare_obj, NPY_UINT8, 2, "compare");
  if (compare == NULL || require_batch_rows(compare, batch_size, "compare") != 0) {
    return NULL;
  }
  if (PyArray_DIM(compare, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "compare second dim too small for MslCompare");
    return NULL;
  }

  PyArrayObject* viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
  if (viewpoint == NULL || require_batch_rows(viewpoint, batch_size, "viewpoint") != 0) {
    return NULL;
  }

  PyArrayObject* gamestate = require_contiguous_array(gamestate_obj, NPY_UINT8, 3, "gamestate");
  if (gamestate == NULL ||
      require_sequence_rows(gamestate, horizon + 1, batch_size, "gamestate") != 0) {
    return NULL;
  }
  if (PyArray_DIM(gamestate, 2) < (npy_intp)sizeof(MeleeGamestate)) {
    PyErr_SetString(PyExc_ValueError, "gamestate third dim too small for MeleeGamestate");
    return NULL;
  }

  PyArrayObject* terminal = require_contiguous_array(terminal_obj, NPY_UINT8, 3, "terminal");
  if (terminal == NULL || require_sequence_rows(terminal, horizon, batch_size, "terminal") != 0) {
    return NULL;
  }
  if (PyArray_DIM(terminal, 2) < (npy_intp)sizeof(MslTerminal)) {
    PyErr_SetString(PyExc_ValueError, "terminal third dim too small for MslTerminal");
    return NULL;
  }

  PyArrayObject* done = require_contiguous_array(done_obj, NPY_UINT8, 2, "done");
  if (done == NULL || require_exact_2d_shape(done, horizon, batch_size, "done") != 0) {
    return NULL;
  }

  PyArrayObject* reset_mask = require_contiguous_array(reset_mask_obj, NPY_UINT8, 2, "reset_mask");
  if (reset_mask == NULL ||
      require_exact_2d_shape(reset_mask, horizon, batch_size, "reset_mask") != 0) {
    return NULL;
  }

  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);

  Py_INCREF(match_config_obj);
  h->match_config_obj = match_config_obj;
  h->match_config_bytes = (const uint8_t*)PyArray_DATA(match_config);
  h->match_config_stride = (size_t)PyArray_STRIDE(match_config, 0);

  Py_INCREF(compare_obj);
  h->compare_obj = compare_obj;
  h->compare_bytes = (uint8_t*)PyArray_DATA(compare);
  h->compare_stride = (size_t)PyArray_STRIDE(compare, 0);

  Py_INCREF(action_obj);
  h->rollout_action_obj = action_obj;
  h->rollout_horizon = horizon;
  h->rollout_action_format = action_format_id;
  h->rollout_action_bytes = (const uint8_t*)PyArray_DATA(action);
  h->rollout_action_frame_stride = (size_t)PyArray_STRIDE(action, 0);
  h->rollout_action_batch_stride = (size_t)PyArray_STRIDE(action, 1);

  Py_INCREF(viewpoint_obj);
  h->rollout_viewpoint_obj = viewpoint_obj;
  h->rollout_viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
  h->rollout_viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);

  Py_INCREF(gamestate_obj);
  h->rollout_gamestate_obj = gamestate_obj;
  h->rollout_gamestate_bytes = (uint8_t*)PyArray_DATA(gamestate);
  h->rollout_gamestate_frame_stride = (size_t)PyArray_STRIDE(gamestate, 0);
  h->rollout_gamestate_batch_stride = (size_t)PyArray_STRIDE(gamestate, 1);

  Py_INCREF(terminal_obj);
  h->rollout_terminal_obj = terminal_obj;
  h->rollout_terminal_bytes = (uint8_t*)PyArray_DATA(terminal);
  h->rollout_terminal_frame_stride = (size_t)PyArray_STRIDE(terminal, 0);
  h->rollout_terminal_batch_stride = (size_t)PyArray_STRIDE(terminal, 1);

  Py_INCREF(done_obj);
  h->rollout_done_obj = done_obj;
  h->rollout_done_bytes = (uint8_t*)PyArray_DATA(done);
  h->rollout_done_frame_stride = (size_t)PyArray_STRIDE(done, 0);
  h->rollout_done_batch_stride = (size_t)PyArray_STRIDE(done, 1);

  Py_INCREF(reset_mask_obj);
  h->rollout_reset_mask_obj = reset_mask_obj;
  h->rollout_reset_mask_bytes = (const uint8_t*)PyArray_DATA(reset_mask);
  h->rollout_reset_mask_frame_stride = (size_t)PyArray_STRIDE(reset_mask, 0);
  h->rollout_reset_mask_batch_stride = (size_t)PyArray_STRIDE(reset_mask, 1);

  memset(h->prev_input_storage, 0, (size_t)batch_size * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)batch_size * h->input_storage_stride);

  Py_RETURN_NONE;
}

static PyObject* msl_unbind_sequence_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  Py_RETURN_NONE;
}

static int msl_require_sequence_buffers(PyMslHandle* h) {
  if (h->match_config_bytes == NULL || h->rollout_action_bytes == NULL ||
      h->prev_input_storage == NULL || h->input_storage == NULL || h->rollout_done_bytes == NULL ||
      h->rollout_reset_mask_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "sequence buffers are not bound");
    return -1;
  }
  return 0;
}

static int msl_require_sequence_frame(PyMslHandle* h, int frame) {
  if (frame < 0 || frame >= h->rollout_horizon) {
    PyErr_Format(PyExc_IndexError, "sequence frame %d is outside length %d", frame,
                 h->rollout_horizon);
    return -1;
  }
  return 0;
}

static const uint8_t* pymsl_prepare_sequence_input_frame(PyMslHandle* h, int frame) {
  const int batch_size = msl_batch_batch_size(h->batch);
  const uint8_t* action_frame =
      h->rollout_action_bytes + (size_t)frame * h->rollout_action_frame_stride;
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_RAW) {
    return action_frame;
  }
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_CONTROLLER) {
    for (int bi = 0; bi < batch_size; bi++) {
      const PyMslControllerInput* src =
          (const PyMslControllerInput*)(action_frame + (size_t)bi * h->rollout_action_batch_stride);
      MslInput* dst = (MslInput*)(h->input_storage + (size_t)bi * h->input_storage_stride);
      pymsl_controller_to_input(src, dst);
    }
    return h->input_storage;
  }
  PyErr_SetString(PyExc_ValueError, "unsupported sequence action format");
  return NULL;
}

static size_t pymsl_prepared_sequence_input_stride(PyMslHandle* h) {
  return h->rollout_action_format == PYMSL_ACTION_FORMAT_RAW ? h->rollout_action_batch_stride
                                                             : h->input_storage_stride;
}

static void pymsl_copy_input_frame_to_prev(PyMslHandle* h, const uint8_t* input_frame,
                                           size_t input_stride) {
  const int batch_size = msl_batch_batch_size(h->batch);
  for (int bi = 0; bi < batch_size; bi++) {
    memcpy(h->prev_input_storage + (size_t)bi * h->prev_input_storage_stride,
           input_frame + (size_t)bi * input_stride, sizeof(MslInput));
  }
}

static void pymsl_clear_prev_input_masked(PyMslHandle* h, const uint8_t* mask_bytes,
                                          size_t mask_stride) {
  const int batch_size = msl_batch_batch_size(h->batch);
  for (int bi = 0; bi < batch_size; bi++) {
    if (*(const uint8_t*)(mask_bytes + (size_t)bi * mask_stride) != 0u) {
      memset(h->prev_input_storage + (size_t)bi * h->prev_input_storage_stride, 0,
             h->prev_input_storage_stride);
    }
  }
}

static PyObject* msl_reset_prev_input(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  memset(h->prev_input_storage, 0, (size_t)batch_size * h->prev_input_storage_stride);
  Py_RETURN_NONE;
}

static PyObject* msl_set_prev_input_from_sequence(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  if (!PyArg_ParseTuple(args, "Oi", &handle_obj, &frame)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }
  const uint8_t* input_frame =
      h->rollout_action_bytes + (size_t)frame * h->rollout_action_frame_stride;
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_CONTROLLER) {
    input_frame = pymsl_prepare_sequence_input_frame(h, frame);
    if (input_frame == NULL) {
      return NULL;
    }
    pymsl_copy_input_frame_to_prev(h, input_frame, h->input_storage_stride);
  } else {
    pymsl_copy_input_frame_to_prev(h, input_frame, h->rollout_action_batch_stride);
  }
  Py_RETURN_NONE;
}

static int msl_require_bound_step_buffers(PyMslHandle* h) {
  if (h->prev_input_bytes == NULL || h->input_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "input buffers are not bound");
    return -1;
  }
  return 0;
}

static PyObject* msl_init_match_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->match_config_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "match_config buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_init_match(h->batch, h->match_config_bytes, h->match_config_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);
  Py_RETURN_NONE;
}

static PyObject* msl_init_match_sequence_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0) {
    return NULL;
  }
  int err = msl_batch_init_match(h->batch, h->match_config_bytes, h->match_config_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);
  if (h->rollout_gamestate_bytes != NULL) {
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, h->rollout_gamestate_bytes,
                                      h->rollout_gamestate_batch_stride);
    if (err != 0) {
      PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
      return NULL;
    }
  }
  Py_RETURN_NONE;
}

static PyObject* msl_reset_sequence_masked(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  int write_initial_observation = 1;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &frame, &write_initial_observation)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }
  const uint8_t* mask_frame =
      h->rollout_reset_mask_bytes + (size_t)frame * h->rollout_reset_mask_frame_stride;
  int err = msl_batch_init_match_masked(h->batch, h->match_config_bytes, h->match_config_stride,
                                        mask_frame, h->rollout_reset_mask_batch_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match_masked failed: %d", err);
    return NULL;
  }
  pymsl_clear_prev_input_masked(h, mask_frame, h->rollout_reset_mask_batch_stride);
  if (write_initial_observation != 0) {
    uint8_t* obs_frame =
        h->rollout_gamestate_bytes + (size_t)frame * h->rollout_gamestate_frame_stride;
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, obs_frame,
                                      h->rollout_gamestate_batch_stride);
    if (err != 0) {
      PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
      return NULL;
    }
  }
  Py_RETURN_NONE;
}

static PyObject* msl_step_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_bound_step_buffers(h) != 0) {
    return NULL;
  }
  int err = 0;
  err = msl_batch_step_input(h->batch, h->prev_input_bytes, h->prev_input_stride, h->input_bytes,
                             h->input_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_write_compare_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->compare_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "compare buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_compare failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_step_write_compare_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_bound_step_buffers(h) != 0) {
    return NULL;
  }
  if (h->compare_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "compare buffer is not bound");
    return NULL;
  }
  int err = 0;
  err = msl_batch_step_input(h->batch, h->prev_input_bytes, h->prev_input_stride, h->input_bytes,
                             h->input_stride);
  if (err == 0) {
    err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  }
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch step/write_compare failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_step_sequence(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  int write_outputs = 1;
  int write_compare = 0;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "Oi|iii", &handle_obj, &frame, &write_outputs, &write_compare,
                        &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }

  const uint8_t* input_frame = pymsl_prepare_sequence_input_frame(h, frame);
  if (input_frame == NULL) {
    return NULL;
  }
  const size_t input_stride = pymsl_prepared_sequence_input_stride(h);
  int err = msl_batch_step_input(h->batch, h->prev_input_storage, h->prev_input_storage_stride,
                                 input_frame, input_stride);
  if (err == 0) {
    pymsl_copy_input_frame_to_prev(h, input_frame, input_stride);
  }
  if (err == 0 && write_outputs != 0) {
    uint8_t* obs_frame =
        h->rollout_gamestate_bytes + (size_t)(frame + 1) * h->rollout_gamestate_frame_stride;
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, obs_frame,
                                      h->rollout_gamestate_batch_stride);
  }
  if (err == 0 && write_outputs != 0) {
    uint8_t* terminal_frame =
        h->rollout_terminal_bytes + (size_t)frame * h->rollout_terminal_frame_stride;
    err = msl_batch_write_terminal(h->batch, terminal_frame, h->rollout_terminal_batch_stride,
                                   (int32_t)max_frame_id);
    if (err == 0) {
      uint8_t* done_frame = h->rollout_done_bytes + (size_t)frame * h->rollout_done_frame_stride;
      for (int bi = 0; bi < msl_batch_batch_size(h->batch); bi++) {
        const MslTerminal* terminal =
            (const MslTerminal*)(terminal_frame + (size_t)bi * h->rollout_terminal_batch_stride);
        *(uint8_t*)(done_frame + (size_t)bi * h->rollout_done_batch_stride) = terminal->done;
      }
    }
  }
  if (err == 0 && write_compare != 0) {
    err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  }
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch sequence step failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_write_gamestate_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->viewpoint_bytes == NULL || h->gamestate_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "viewpoint and gamestate buffers must be bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = melee_batch_write_gamestate(h->batch, h->viewpoint_bytes, h->viewpoint_stride,
                                    h->gamestate_bytes, h->gamestate_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_write_terminal_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "O|i", &handle_obj, &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->terminal_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "terminal buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_write_terminal(h->batch, h->terminal_bytes, h->terminal_stride,
                                 (int32_t)max_frame_id);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_terminal failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

enum {
  MSL_DISR_FIELD_ACTION_ID = 1,
  MSL_DISR_FIELD_ANIMATION_INDEX = 2,
  MSL_DISR_FIELD_ACTION_FRAME = 3,
  MSL_DISR_FIELD_ON_GROUND = 4,
  MSL_DISR_FIELD_HITLAG = 5,
  MSL_DISR_FIELD_HITSTUN = 6,
  MSL_DISR_FIELD_STATE_FLAGS = 7,
  MSL_DISR_FIELD_JUMPS_LEFT = 8,
  MSL_DISR_FIELD_STOCKS = 9,
  MSL_DISR_FIELD_IS_DEAD = 10,
  MSL_DISR_FIELD_HURTBOX_STATE = 11,
  MSL_DISR_FIELD_GROUND_ID = 12,
  MSL_DISR_FIELD_INSTANCE_ID = 13,
  MSL_DISR_FIELD_INSTANCE_HIT_BY = 14,
  MSL_DISR_FIELD_LAST_ATTACK_LANDED = 15,
  MSL_DISR_FIELD_COMBO_COUNT = 16,
  MSL_DISR_FIELD_LAST_HIT_BY = 17,
  MSL_DISR_FIELD_L_CANCEL = 18,
  MSL_DISR_FIELD_FACING = 19,
  MSL_DISR_FIELD_POS_X = 20,
  MSL_DISR_FIELD_POS_Y = 21,
  MSL_DISR_FIELD_SPEED_AIR_X_SELF = 22,
  MSL_DISR_FIELD_SPEED_GROUND_X_SELF = 23,
  MSL_DISR_FIELD_SPEED_Y_SELF = 24,
  MSL_DISR_FIELD_SPEED_X_ATTACK = 25,
  MSL_DISR_FIELD_SPEED_Y_ATTACK = 26,
  MSL_DISR_FIELD_PERCENT = 27,
  MSL_DISR_FIELD_SHIELD_HP = 28,
  MSL_DISR_FIELD_STATE_FLAGS_0 = 100,
  MSL_DISR_FIELD_STATE_FLAGS_1 = 101,
  MSL_DISR_FIELD_STATE_FLAGS_2 = 102,
  MSL_DISR_FIELD_STATE_FLAGS_3 = 103,
  MSL_DISR_FIELD_STATE_FLAGS_4 = 104,
  MSL_DISR_FIELD_ITEM_EXISTS = 200,
  MSL_DISR_FIELD_ITEM_STATE = 201,
  MSL_DISR_FIELD_ITEM_TYPE = 202,
  MSL_DISR_FIELD_ITEM_OWNER = 203,
  MSL_DISR_FIELD_ITEM_INSTANCE_ID = 204,
  MSL_DISR_FIELD_ITEM_POS_X = 205,
  MSL_DISR_FIELD_ITEM_POS_Y = 206,
  MSL_DISR_FIELD_ITEM_VEL_X = 207,
  MSL_DISR_FIELD_ITEM_VEL_Y = 208,
};

enum {
  MSL_DISR_MASK_ACTION_ID = 0,
  MSL_DISR_MASK_ANIMATION_INDEX = 1,
  MSL_DISR_MASK_ACTION_FRAME = 2,
  MSL_DISR_MASK_ON_GROUND = 3,
  MSL_DISR_MASK_HITLAG = 4,
  MSL_DISR_MASK_HITSTUN = 5,
  MSL_DISR_MASK_STATE_FLAGS_0 = 6,
  MSL_DISR_MASK_STATE_FLAGS_1 = 7,
  MSL_DISR_MASK_STATE_FLAGS_2 = 8,
  MSL_DISR_MASK_STATE_FLAGS_3 = 9,
  MSL_DISR_MASK_STATE_FLAGS_4 = 10,
  MSL_DISR_MASK_JUMPS_LEFT = 11,
  MSL_DISR_MASK_STOCKS = 12,
  MSL_DISR_MASK_IS_DEAD = 13,
  MSL_DISR_MASK_HURTBOX_STATE = 14,
  MSL_DISR_MASK_GROUND_ID = 15,
  MSL_DISR_MASK_INSTANCE_ID = 16,
  MSL_DISR_MASK_INSTANCE_HIT_BY = 17,
  MSL_DISR_MASK_LAST_ATTACK_LANDED = 18,
  MSL_DISR_MASK_COMBO_COUNT = 19,
  MSL_DISR_MASK_LAST_HIT_BY = 20,
  MSL_DISR_MASK_L_CANCEL = 21,
  MSL_DISR_MASK_FACING = 22,
  MSL_DISR_MASK_POS_X = 23,
  MSL_DISR_MASK_POS_Y = 24,
  MSL_DISR_MASK_SPEED_AIR_X_SELF = 25,
  MSL_DISR_MASK_SPEED_GROUND_X_SELF = 26,
  MSL_DISR_MASK_SPEED_Y_SELF = 27,
  MSL_DISR_MASK_SPEED_X_ATTACK = 28,
  MSL_DISR_MASK_SPEED_Y_ATTACK = 29,
  MSL_DISR_MASK_PERCENT = 30,
  MSL_DISR_MASK_SHIELD_HP = 31,
};

typedef struct MslDisruptiveFirstMismatch {
  int32_t offset;
  int32_t player;
  int32_t field_code;
  int32_t subindex;
  int32_t kind;  // 0=int, 1=float
  int64_t out_i;
  int64_t ref_i;
  double out_f;
  double ref_f;
  uint8_t valid;
} MslDisruptiveFirstMismatch;

typedef struct MslDisruptiveNativeRow {
  int64_t record;
  int64_t seed_frame;
  int64_t horizon;
  int64_t ref_frame;
  int64_t player;
  int64_t first_offset;
  int64_t first_field_code;
  int64_t first_subindex;
  int64_t first_player;
  int64_t first_kind;
  int64_t first_out_i;
  int64_t first_ref_i;
  int64_t seed_action_id;
  int64_t out_action_id;
  int64_t ref_action_id;
  int64_t seed_action_frame;
  int64_t out_action_frame;
  int64_t ref_action_frame;
  int64_t on_ground;
  int64_t hitlag;
  int64_t hitstun;
  uint64_t family_field_mask;
  double first_out_f;
  double first_ref_f;
  double score_total;
  double score_discrete;
  double score_float;
  double score_item;
} MslDisruptiveNativeRow;

typedef struct MslDisruptiveRowVec {
  MslDisruptiveNativeRow* data;
  size_t len;
  size_t cap;
} MslDisruptiveRowVec;

static int msl_disr_field_from_name(const char* name, int float_field) {
  if (strcmp(name, "action_id") == 0) return MSL_DISR_FIELD_ACTION_ID;
  if (strcmp(name, "animation_index") == 0) return MSL_DISR_FIELD_ANIMATION_INDEX;
  if (strcmp(name, "action_frame") == 0) return MSL_DISR_FIELD_ACTION_FRAME;
  if (strcmp(name, "on_ground") == 0) return MSL_DISR_FIELD_ON_GROUND;
  if (strcmp(name, "hitlag") == 0) return MSL_DISR_FIELD_HITLAG;
  if (strcmp(name, "hitstun") == 0) return MSL_DISR_FIELD_HITSTUN;
  if (strcmp(name, "state_flags") == 0) return MSL_DISR_FIELD_STATE_FLAGS;
  if (strcmp(name, "jumps_left") == 0) return MSL_DISR_FIELD_JUMPS_LEFT;
  if (strcmp(name, "stocks") == 0) return MSL_DISR_FIELD_STOCKS;
  if (strcmp(name, "is_dead") == 0) return MSL_DISR_FIELD_IS_DEAD;
  if (strcmp(name, "hurtbox_state") == 0) return MSL_DISR_FIELD_HURTBOX_STATE;
  if (strcmp(name, "ground_id") == 0) return MSL_DISR_FIELD_GROUND_ID;
  if (strcmp(name, "instance_id") == 0) return MSL_DISR_FIELD_INSTANCE_ID;
  if (strcmp(name, "instance_hit_by") == 0) return MSL_DISR_FIELD_INSTANCE_HIT_BY;
  if (strcmp(name, "last_attack_landed") == 0) return MSL_DISR_FIELD_LAST_ATTACK_LANDED;
  if (strcmp(name, "combo_count") == 0) return MSL_DISR_FIELD_COMBO_COUNT;
  if (strcmp(name, "last_hit_by") == 0) return MSL_DISR_FIELD_LAST_HIT_BY;
  if (strcmp(name, "l_cancel") == 0) return MSL_DISR_FIELD_L_CANCEL;
  if (strcmp(name, "facing") == 0) return MSL_DISR_FIELD_FACING;
  if (strcmp(name, "pos_x") == 0) return MSL_DISR_FIELD_POS_X;
  if (strcmp(name, "pos_y") == 0) return MSL_DISR_FIELD_POS_Y;
  if (strcmp(name, "speed_air_x_self") == 0) return MSL_DISR_FIELD_SPEED_AIR_X_SELF;
  if (strcmp(name, "speed_ground_x_self") == 0) return MSL_DISR_FIELD_SPEED_GROUND_X_SELF;
  if (strcmp(name, "speed_y_self") == 0) return MSL_DISR_FIELD_SPEED_Y_SELF;
  if (strcmp(name, "speed_x_attack") == 0) return MSL_DISR_FIELD_SPEED_X_ATTACK;
  if (strcmp(name, "speed_y_attack") == 0) return MSL_DISR_FIELD_SPEED_Y_ATTACK;
  if (strcmp(name, "percent") == 0) return MSL_DISR_FIELD_PERCENT;
  if (strcmp(name, "shield_hp") == 0) return MSL_DISR_FIELD_SHIELD_HP;
  (void)float_field;
  return 0;
}

static int msl_disr_parse_field_sequence(PyObject* obj, int* out, int max_count, int float_fields,
                                         const char* name) {
  PyObject* seq = PySequence_Fast(obj, name);
  if (seq == NULL) {
    return -1;
  }
  const Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
  if (n < 0 || n > max_count) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError, "%s has unsupported length", name);
    return -1;
  }
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject* item = PySequence_Fast_GET_ITEM(seq, i);
    const char* s = PyUnicode_AsUTF8(item);
    if (s == NULL) {
      Py_DECREF(seq);
      return -1;
    }
    const int field = msl_disr_field_from_name(s, float_fields);
    if (field == 0) {
      Py_DECREF(seq);
      PyErr_Format(PyExc_ValueError, "unknown %s field: %s", name, s);
      return -1;
    }
    out[i] = field;
  }
  Py_DECREF(seq);
  return (int)n;
}

static int msl_disr_parse_i32_sequence(PyObject* obj, int* out, int max_count, const char* name) {
  PyObject* seq = PySequence_Fast(obj, name);
  if (seq == NULL) {
    return -1;
  }
  const Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
  if (n < 0 || n > max_count) {
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError, "%s has unsupported length", name);
    return -1;
  }
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject* item = PySequence_Fast_GET_ITEM(seq, i);
    const long v = PyLong_AsLong(item);
    if (PyErr_Occurred()) {
      Py_DECREF(seq);
      return -1;
    }
    out[i] = (int)v;
  }
  Py_DECREF(seq);
  return (int)n;
}

static int64_t msl_disr_compare_int_field(const MslCompare* row, int field, int p, int sub) {
  switch (field) {
    case MSL_DISR_FIELD_ACTION_ID:
      return (int64_t)row->action_id[p];
    case MSL_DISR_FIELD_ANIMATION_INDEX:
      return (int64_t)row->animation_index[p];
    case MSL_DISR_FIELD_ACTION_FRAME:
      return (int64_t)row->action_frame[p];
    case MSL_DISR_FIELD_ON_GROUND:
      return (int64_t)row->on_ground[p];
    case MSL_DISR_FIELD_HITLAG:
      return (int64_t)row->hitlag[p];
    case MSL_DISR_FIELD_HITSTUN:
      return (int64_t)row->hitstun[p];
    case MSL_DISR_FIELD_STATE_FLAGS:
      return (sub >= 0 && sub < MSL_STATE_FLAGS_BYTES) ? (int64_t)row->state_flags[p][sub] : 0;
    case MSL_DISR_FIELD_JUMPS_LEFT:
      return (int64_t)row->jumps_left[p];
    case MSL_DISR_FIELD_STOCKS:
      return (int64_t)row->stocks[p];
    case MSL_DISR_FIELD_IS_DEAD:
      return (int64_t)row->is_dead[p];
    case MSL_DISR_FIELD_HURTBOX_STATE:
      return (int64_t)row->hurtbox_state[p];
    case MSL_DISR_FIELD_GROUND_ID:
      return (int64_t)row->ground_id[p];
    case MSL_DISR_FIELD_INSTANCE_ID:
      return (int64_t)row->instance_id[p];
    case MSL_DISR_FIELD_INSTANCE_HIT_BY:
      return (int64_t)row->instance_hit_by[p];
    case MSL_DISR_FIELD_LAST_ATTACK_LANDED:
      return (int64_t)row->last_attack_landed[p];
    case MSL_DISR_FIELD_COMBO_COUNT:
      return (int64_t)row->combo_count[p];
    case MSL_DISR_FIELD_LAST_HIT_BY:
      return (int64_t)row->last_hit_by[p];
    case MSL_DISR_FIELD_L_CANCEL:
      return (int64_t)row->l_cancel[p];
    case MSL_DISR_FIELD_FACING:
      return (int64_t)row->facing[p];
    default:
      return 0;
  }
}

static double msl_disr_compare_float_field(const MslCompare* row, int field, int p) {
  switch (field) {
    case MSL_DISR_FIELD_POS_X:
      return (double)row->pos_x[p];
    case MSL_DISR_FIELD_POS_Y:
      return (double)row->pos_y[p];
    case MSL_DISR_FIELD_SPEED_AIR_X_SELF:
      return (double)row->speed_air_x_self[p];
    case MSL_DISR_FIELD_SPEED_GROUND_X_SELF:
      return (double)row->speed_ground_x_self[p];
    case MSL_DISR_FIELD_SPEED_Y_SELF:
      return (double)row->speed_y_self[p];
    case MSL_DISR_FIELD_SPEED_X_ATTACK:
      return (double)row->speed_x_attack[p];
    case MSL_DISR_FIELD_SPEED_Y_ATTACK:
      return (double)row->speed_y_attack[p];
    case MSL_DISR_FIELD_PERCENT:
      return (double)row->percent[p];
    case MSL_DISR_FIELD_SHIELD_HP:
      return (double)row->shield_hp[p];
    default:
      return 0.0;
  }
}

static uint64_t msl_disr_mask_for_field(int field, int sub) {
  int bit = -1;
  switch (field) {
    case MSL_DISR_FIELD_ACTION_ID:
      bit = MSL_DISR_MASK_ACTION_ID;
      break;
    case MSL_DISR_FIELD_ANIMATION_INDEX:
      bit = MSL_DISR_MASK_ANIMATION_INDEX;
      break;
    case MSL_DISR_FIELD_ACTION_FRAME:
      bit = MSL_DISR_MASK_ACTION_FRAME;
      break;
    case MSL_DISR_FIELD_ON_GROUND:
      bit = MSL_DISR_MASK_ON_GROUND;
      break;
    case MSL_DISR_FIELD_HITLAG:
      bit = MSL_DISR_MASK_HITLAG;
      break;
    case MSL_DISR_FIELD_HITSTUN:
      bit = MSL_DISR_MASK_HITSTUN;
      break;
    case MSL_DISR_FIELD_STATE_FLAGS:
      bit = MSL_DISR_MASK_STATE_FLAGS_0 + sub;
      break;
    case MSL_DISR_FIELD_JUMPS_LEFT:
      bit = MSL_DISR_MASK_JUMPS_LEFT;
      break;
    case MSL_DISR_FIELD_STOCKS:
      bit = MSL_DISR_MASK_STOCKS;
      break;
    case MSL_DISR_FIELD_IS_DEAD:
      bit = MSL_DISR_MASK_IS_DEAD;
      break;
    case MSL_DISR_FIELD_HURTBOX_STATE:
      bit = MSL_DISR_MASK_HURTBOX_STATE;
      break;
    case MSL_DISR_FIELD_GROUND_ID:
      bit = MSL_DISR_MASK_GROUND_ID;
      break;
    case MSL_DISR_FIELD_INSTANCE_ID:
      bit = MSL_DISR_MASK_INSTANCE_ID;
      break;
    case MSL_DISR_FIELD_INSTANCE_HIT_BY:
      bit = MSL_DISR_MASK_INSTANCE_HIT_BY;
      break;
    case MSL_DISR_FIELD_LAST_ATTACK_LANDED:
      bit = MSL_DISR_MASK_LAST_ATTACK_LANDED;
      break;
    case MSL_DISR_FIELD_COMBO_COUNT:
      bit = MSL_DISR_MASK_COMBO_COUNT;
      break;
    case MSL_DISR_FIELD_LAST_HIT_BY:
      bit = MSL_DISR_MASK_LAST_HIT_BY;
      break;
    case MSL_DISR_FIELD_L_CANCEL:
      bit = MSL_DISR_MASK_L_CANCEL;
      break;
    case MSL_DISR_FIELD_FACING:
      bit = MSL_DISR_MASK_FACING;
      break;
    case MSL_DISR_FIELD_POS_X:
      bit = MSL_DISR_MASK_POS_X;
      break;
    case MSL_DISR_FIELD_POS_Y:
      bit = MSL_DISR_MASK_POS_Y;
      break;
    case MSL_DISR_FIELD_SPEED_AIR_X_SELF:
      bit = MSL_DISR_MASK_SPEED_AIR_X_SELF;
      break;
    case MSL_DISR_FIELD_SPEED_GROUND_X_SELF:
      bit = MSL_DISR_MASK_SPEED_GROUND_X_SELF;
      break;
    case MSL_DISR_FIELD_SPEED_Y_SELF:
      bit = MSL_DISR_MASK_SPEED_Y_SELF;
      break;
    case MSL_DISR_FIELD_SPEED_X_ATTACK:
      bit = MSL_DISR_MASK_SPEED_X_ATTACK;
      break;
    case MSL_DISR_FIELD_SPEED_Y_ATTACK:
      bit = MSL_DISR_MASK_SPEED_Y_ATTACK;
      break;
    case MSL_DISR_FIELD_PERCENT:
      bit = MSL_DISR_MASK_PERCENT;
      break;
    case MSL_DISR_FIELD_SHIELD_HP:
      bit = MSL_DISR_MASK_SHIELD_HP;
      break;
    default:
      break;
  }
  return bit >= 0 ? (UINT64_C(1) << (unsigned)bit) : 0u;
}

static int msl_disr_first_field_code(int field, int sub) {
  if (field == MSL_DISR_FIELD_STATE_FLAGS) {
    return MSL_DISR_FIELD_STATE_FLAGS_0 + sub;
  }
  return field;
}

static uint64_t msl_disr_scored_int(uint64_t v, int field, int sub, int profile_rl1) {
  if (profile_rl1 && field == MSL_DISR_FIELD_STATE_FLAGS && sub == 4) {
    return v & ~UINT64_C(0x80);
  }
  return v;
}

static int msl_disr_ints_differ(int64_t out, int64_t ref, int field, int sub, int profile_rl1) {
  return msl_disr_scored_int((uint64_t)out, field, sub, profile_rl1) !=
         msl_disr_scored_int((uint64_t)ref, field, sub, profile_rl1);
}

static double msl_disr_clamp(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static double msl_disr_discrete_score(int field, int64_t out, int64_t ref) {
  if (out == ref) return 0.0;
  if (field == MSL_DISR_FIELD_ACTION_FRAME) {
    return 6.0 * msl_disr_clamp(fabs((double)out - (double)ref), 0.0, 10.0);
  }
  if (field == MSL_DISR_FIELD_HITLAG) {
    return 25.0 * msl_disr_clamp(fabs((double)out - (double)ref), 0.0, 10.0);
  }
  if (field == MSL_DISR_FIELD_HITSTUN) {
    return 12.0 * msl_disr_clamp(fabs((double)out - (double)ref), 0.0, 20.0);
  }
  if (field == MSL_DISR_FIELD_STOCKS) {
    return 1000.0 * fabs((double)out - (double)ref);
  }
  switch (field) {
    case MSL_DISR_FIELD_ACTION_ID:
      return 140.0;
    case MSL_DISR_FIELD_ANIMATION_INDEX:
      return 120.0;
    case MSL_DISR_FIELD_ON_GROUND:
      return 90.0;
    case MSL_DISR_FIELD_IS_DEAD:
      return 300.0;
    case MSL_DISR_FIELD_JUMPS_LEFT:
      return 30.0;
    case MSL_DISR_FIELD_L_CANCEL:
      return 20.0;
    case MSL_DISR_FIELD_HURTBOX_STATE:
      return 30.0;
    case MSL_DISR_FIELD_GROUND_ID:
      return 20.0;
    case MSL_DISR_FIELD_INSTANCE_ID:
      return 20.0;
    case MSL_DISR_FIELD_INSTANCE_HIT_BY:
      return 40.0;
    case MSL_DISR_FIELD_LAST_ATTACK_LANDED:
      return 30.0;
    case MSL_DISR_FIELD_COMBO_COUNT:
      return 25.0;
    case MSL_DISR_FIELD_LAST_HIT_BY:
      return 30.0;
    case MSL_DISR_FIELD_STATE_FLAGS:
      return 15.0;
    default:
      return 10.0;
  }
}

static void msl_disr_float_weight(int field, double* weight, double* cap) {
  if (field == MSL_DISR_FIELD_POS_X || field == MSL_DISR_FIELD_POS_Y) {
    *weight = 12.0;
    *cap = 20.0;
  } else if (field == MSL_DISR_FIELD_PERCENT) {
    *weight = 40.0;
    *cap = 30.0;
  } else if (field == MSL_DISR_FIELD_SHIELD_HP) {
    *weight = 20.0;
    *cap = 15.0;
  } else {
    *weight = 4.0;
    *cap = 20.0;
  }
}

static int msl_disr_vec_push(MslDisruptiveRowVec* vec, const MslDisruptiveNativeRow* row) {
  if (vec->len == vec->cap) {
    size_t next = vec->cap == 0 ? 4096u : vec->cap * 2u;
    MslDisruptiveNativeRow* data =
        (MslDisruptiveNativeRow*)PyMem_RawRealloc(vec->data, next * sizeof(*data));
    if (data == NULL) {
      return -1;
    }
    vec->data = data;
    vec->cap = next;
  }
  vec->data[vec->len++] = *row;
  return 0;
}

static MslDisruptiveFirstMismatch msl_disr_first_mismatch(
    const MslCompare* out, const MslCompare* ref, const int* players, int player_count,
    const int* discrete_fields, int discrete_count, const int* float_fields, int float_count,
    int offset, double float_epsilon, int profile_rl1) {
  MslDisruptiveFirstMismatch first;
  memset(&first, 0, sizeof(first));
  first.offset = offset;
  first.player = -1;
  first.subindex = -1;

  for (int fi = 0; fi < discrete_count; fi++) {
    const int field = discrete_fields[fi];
    if (field == MSL_DISR_FIELD_STATE_FLAGS) {
      for (int pi = 0; pi < player_count; pi++) {
        const int p = players[pi];
        for (int sub = 0; sub < MSL_STATE_FLAGS_BYTES; sub++) {
          const int64_t ov = msl_disr_compare_int_field(out, field, p, sub);
          const int64_t rv = msl_disr_compare_int_field(ref, field, p, sub);
          if (msl_disr_ints_differ(ov, rv, field, sub, profile_rl1)) {
            first.valid = 1u;
            first.player = p;
            first.field_code = msl_disr_first_field_code(field, sub);
            first.subindex = sub;
            first.kind = 0;
            first.out_i = ov;
            first.ref_i = rv;
            return first;
          }
        }
      }
      continue;
    }
    for (int pi = 0; pi < player_count; pi++) {
      const int p = players[pi];
      const int64_t ov = msl_disr_compare_int_field(out, field, p, -1);
      const int64_t rv = msl_disr_compare_int_field(ref, field, p, -1);
      if (msl_disr_ints_differ(ov, rv, field, -1, profile_rl1)) {
        first.valid = 1u;
        first.player = p;
        first.field_code = field;
        first.subindex = -1;
        first.kind = 0;
        first.out_i = ov;
        first.ref_i = rv;
        return first;
      }
    }
  }

  for (int fi = 0; fi < float_count; fi++) {
    const int field = float_fields[fi];
    for (int pi = 0; pi < player_count; pi++) {
      const int p = players[pi];
      const double ov = msl_disr_compare_float_field(out, field, p);
      const double rv = msl_disr_compare_float_field(ref, field, p);
      if (fabs(ov - rv) > float_epsilon) {
        first.valid = 1u;
        first.player = p;
        first.field_code = field;
        first.subindex = -1;
        first.kind = 1;
        first.out_f = ov;
        first.ref_f = rv;
        return first;
      }
    }
  }

  for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
    const MslItem* oi = &out->items[slot];
    const MslItem* ri = &ref->items[slot];
    if (oi->exists != ri->exists) {
      first.valid = 1u;
      first.player = -1;
      first.field_code = MSL_DISR_FIELD_ITEM_EXISTS;
      first.subindex = slot;
      first.kind = 0;
      first.out_i = oi->exists;
      first.ref_i = ri->exists;
      return first;
    }
    if (oi->state != ri->state) {
      first.valid = 1u;
      first.player = -1;
      first.field_code = MSL_DISR_FIELD_ITEM_STATE;
      first.subindex = slot;
      first.kind = 0;
      first.out_i = oi->state;
      first.ref_i = ri->state;
      return first;
    }
    if (oi->type != ri->type) {
      first.valid = 1u;
      first.player = -1;
      first.field_code = MSL_DISR_FIELD_ITEM_TYPE;
      first.subindex = slot;
      first.kind = 0;
      first.out_i = oi->type;
      first.ref_i = ri->type;
      return first;
    }
    if (oi->owner != ri->owner) {
      first.valid = 1u;
      first.player = -1;
      first.field_code = MSL_DISR_FIELD_ITEM_OWNER;
      first.subindex = slot;
      first.kind = 0;
      first.out_i = oi->owner;
      first.ref_i = ri->owner;
      return first;
    }
    if (oi->instance_id != ri->instance_id) {
      first.valid = 1u;
      first.player = -1;
      first.field_code = MSL_DISR_FIELD_ITEM_INSTANCE_ID;
      first.subindex = slot;
      first.kind = 0;
      first.out_i = oi->instance_id;
      first.ref_i = ri->instance_id;
      return first;
    }
    if (oi->exists || ri->exists) {
      if (fabs((double)oi->pos_x - (double)ri->pos_x) > float_epsilon) {
        first.valid = 1u;
        first.player = -1;
        first.field_code = MSL_DISR_FIELD_ITEM_POS_X;
        first.subindex = slot;
        first.kind = 1;
        first.out_f = oi->pos_x;
        first.ref_f = ri->pos_x;
        return first;
      }
      if (fabs((double)oi->pos_y - (double)ri->pos_y) > float_epsilon) {
        first.valid = 1u;
        first.player = -1;
        first.field_code = MSL_DISR_FIELD_ITEM_POS_Y;
        first.subindex = slot;
        first.kind = 1;
        first.out_f = oi->pos_y;
        first.ref_f = ri->pos_y;
        return first;
      }
      if (fabs((double)oi->vel_x - (double)ri->vel_x) > float_epsilon) {
        first.valid = 1u;
        first.player = -1;
        first.field_code = MSL_DISR_FIELD_ITEM_VEL_X;
        first.subindex = slot;
        first.kind = 1;
        first.out_f = oi->vel_x;
        first.ref_f = ri->vel_x;
        return first;
      }
      if (fabs((double)oi->vel_y - (double)ri->vel_y) > float_epsilon) {
        first.valid = 1u;
        first.player = -1;
        first.field_code = MSL_DISR_FIELD_ITEM_VEL_Y;
        first.subindex = slot;
        first.kind = 1;
        first.out_f = oi->vel_y;
        first.ref_f = ri->vel_y;
        return first;
      }
    }
  }

  return first;
}

static uint64_t msl_disr_mismatched_player_mask(const MslCompare* out, const MslCompare* ref,
                                                int player, const int* discrete_fields,
                                                int discrete_count, const int* float_fields,
                                                int float_count, double float_epsilon,
                                                int profile_rl1) {
  uint64_t mask = 0u;
  for (int fi = 0; fi < discrete_count; fi++) {
    const int field = discrete_fields[fi];
    if (field == MSL_DISR_FIELD_STATE_FLAGS) {
      for (int sub = 0; sub < MSL_STATE_FLAGS_BYTES; sub++) {
        const int64_t ov = msl_disr_compare_int_field(out, field, player, sub);
        const int64_t rv = msl_disr_compare_int_field(ref, field, player, sub);
        if (msl_disr_ints_differ(ov, rv, field, sub, profile_rl1)) {
          mask |= msl_disr_mask_for_field(field, sub);
        }
      }
      continue;
    }
    const int64_t ov = msl_disr_compare_int_field(out, field, player, -1);
    const int64_t rv = msl_disr_compare_int_field(ref, field, player, -1);
    if (msl_disr_ints_differ(ov, rv, field, -1, profile_rl1)) {
      mask |= msl_disr_mask_for_field(field, -1);
    }
  }
  for (int fi = 0; fi < float_count; fi++) {
    const int field = float_fields[fi];
    const double ov = msl_disr_compare_float_field(out, field, player);
    const double rv = msl_disr_compare_float_field(ref, field, player);
    if (fabs(ov - rv) > float_epsilon) {
      mask |= msl_disr_mask_for_field(field, -1);
    }
  }
  return mask;
}

static void msl_disr_score_row(const MslCompare* out, const MslCompare* ref, const int* players,
                               int player_count, const int* discrete_fields, int discrete_count,
                               const int* float_fields, int float_count, double float_epsilon,
                               int profile_rl1, double player_scores[MSL_MAX_PLAYERS],
                               double* out_discrete, double* out_float, double* out_item) {
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    player_scores[p] = 0.0;
  }
  double discrete = 0.0;
  double floats = 0.0;
  double items = 0.0;

  for (int fi = 0; fi < discrete_count; fi++) {
    const int field = discrete_fields[fi];
    if (field == MSL_DISR_FIELD_STATE_FLAGS) {
      for (int pi = 0; pi < player_count; pi++) {
        const int p = players[pi];
        int diff_count = 0;
        for (int sub = 0; sub < MSL_STATE_FLAGS_BYTES; sub++) {
          const int64_t ov = msl_disr_compare_int_field(out, field, p, sub);
          const int64_t rv = msl_disr_compare_int_field(ref, field, p, sub);
          if (msl_disr_ints_differ(ov, rv, field, sub, profile_rl1)) {
            diff_count++;
          }
        }
        const double score = (double)diff_count * 15.0;
        player_scores[p] += score;
        discrete += score;
      }
      continue;
    }
    for (int pi = 0; pi < player_count; pi++) {
      const int p = players[pi];
      const int64_t ov = msl_disr_compare_int_field(out, field, p, -1);
      const int64_t rv = msl_disr_compare_int_field(ref, field, p, -1);
      if (!msl_disr_ints_differ(ov, rv, field, -1, profile_rl1)) {
        continue;
      }
      const double score = msl_disr_discrete_score(field, ov, rv);
      player_scores[p] += score;
      discrete += score;
    }
  }

  for (int fi = 0; fi < float_count; fi++) {
    const int field = float_fields[fi];
    double weight = 0.0, cap = 0.0;
    msl_disr_float_weight(field, &weight, &cap);
    for (int pi = 0; pi < player_count; pi++) {
      const int p = players[pi];
      const double delta = fabs(msl_disr_compare_float_field(out, field, p) -
                                msl_disr_compare_float_field(ref, field, p));
      if (delta <= float_epsilon) {
        continue;
      }
      const double score = weight * msl_disr_clamp(delta, 0.0, cap);
      player_scores[p] += score;
      floats += score;
    }
  }

  for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
    const MslItem* oi = &out->items[slot];
    const MslItem* ri = &ref->items[slot];
    if (oi->exists != ri->exists) items += 100.0;
    if (oi->state != ri->state) items += 60.0;
    if (oi->type != ri->type) items += 60.0;
    if (oi->owner != ri->owner) items += 50.0;
    if (oi->exists || ri->exists) {
      double delta = fabs((double)oi->pos_x - (double)ri->pos_x);
      if (delta > float_epsilon) items += 8.0 * msl_disr_clamp(delta, 0.0, 20.0);
      delta = fabs((double)oi->pos_y - (double)ri->pos_y);
      if (delta > float_epsilon) items += 8.0 * msl_disr_clamp(delta, 0.0, 20.0);
    }
  }

  *out_discrete = discrete;
  *out_float = floats;
  *out_item = items;
}

static int msl_disr_selected_player(const int* players, int player_count,
                                    const double player_scores[MSL_MAX_PLAYERS],
                                    double item_score) {
  int best = players[0];
  for (int i = 1; i < player_count; i++) {
    const int p = players[i];
    if (player_scores[p] > player_scores[best] ||
        (player_scores[p] == player_scores[best] && p < best)) {
      best = p;
    }
  }
  if (player_scores[best] <= 0.0 && item_score > 0.0) {
    return -1;
  }
  return best;
}

static int msl_disr_horizon_contains(const int* horizons, int horizon_count, int offset) {
  for (int i = 0; i < horizon_count; i++) {
    if (horizons[i] == offset) {
      return 1;
    }
  }
  return 0;
}

static void msl_disr_fill_inactive(uint8_t* buf, int active_count, int batch_size, size_t stride,
                                   size_t width) {
  if (active_count <= 0 || active_count >= batch_size) {
    return;
  }
  uint8_t* last = buf + (size_t)(active_count - 1) * stride;
  for (int i = active_count; i < batch_size; i++) {
    memcpy(buf + (size_t)i * stride, last, width);
  }
}

static PyObject* msl_disruptive_scan(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* samples_obj = NULL;
  PyObject* horizons_obj = NULL;
  PyObject* discrete_obj = NULL;
  PyObject* float_obj = NULL;
  PyObject* players_obj = NULL;
  int num_players = 0;
  int max_records = 0;
  int stride_records = 1;
  double float_epsilon = 0.05;
  int ucf_enabled = -1;
  int ucf_cardinals = -1;
  int batch_size = 512;
  int start_record = 0;
  int stop_record = 0;
  int profile_rl1 = 1;
  if (!PyArg_ParseTuple(args, "OOOOOiiidiiiiii", &samples_obj, &horizons_obj, &discrete_obj,
                        &float_obj, &players_obj, &num_players, &max_records, &stride_records,
                        &float_epsilon, &ucf_enabled, &ucf_cardinals, &batch_size, &start_record,
                        &stop_record, &profile_rl1)) {
    return NULL;
  }

  PyArrayObject* samples =
      require_contiguous_array_readonly(samples_obj, NPY_UINT8, 2, "samples_u8");
  if (samples == NULL) {
    return NULL;
  }
  if (PyArray_DIM(samples, 1) < (npy_intp)sizeof(MslSample)) {
    PyErr_SetString(PyExc_ValueError, "samples_u8 second dim too small for MslSample");
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }

  int horizons[16];
  int discrete_fields[32];
  int float_fields[16];
  int players[MSL_MAX_PLAYERS];
  const int horizon_count = msl_disr_parse_i32_sequence(horizons_obj, horizons, 16, "horizons");
  if (horizon_count <= 0) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "horizons must be non-empty");
    return NULL;
  }
  const int discrete_count =
      msl_disr_parse_field_sequence(discrete_obj, discrete_fields, 32, 0, "discrete_fields");
  if (discrete_count < 0) {
    return NULL;
  }
  const int float_count =
      msl_disr_parse_field_sequence(float_obj, float_fields, 16, 1, "float_fields");
  if (float_count < 0) {
    return NULL;
  }
  const int player_count =
      msl_disr_parse_i32_sequence(players_obj, players, MSL_MAX_PLAYERS, "players");
  if (player_count <= 0) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "players must be non-empty");
    return NULL;
  }
  int max_horizon = 0;
  for (int i = 0; i < horizon_count; i++) {
    if (horizons[i] <= 0) {
      PyErr_SetString(PyExc_ValueError, "horizons must be positive");
      return NULL;
    }
    if (horizons[i] > max_horizon) max_horizon = horizons[i];
  }
  for (int i = 0; i < player_count; i++) {
    if (players[i] < 0 || players[i] >= num_players) {
      PyErr_SetString(PyExc_ValueError, "player index out of range");
      return NULL;
    }
  }
  if (batch_size <= 0) batch_size = 1;
  if (stride_records <= 0) stride_records = 1;

  const int total_records = (int)PyArray_DIM(samples, 0);
  int usable_records = total_records - max_horizon + 1;
  if (usable_records < 0) usable_records = 0;
  if (max_records > 0 && usable_records > max_records) usable_records = max_records;
  int record_start = start_record < 0 ? 0 : start_record;
  int record_stop = stop_record <= 0 || stop_record > usable_records ? usable_records : stop_record;
  if (record_start > record_stop) record_start = record_stop;

  MslBatch* batch = msl_batch_create(batch_size, num_players);
  if (batch == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "msl_batch_create failed");
    return NULL;
  }
  if (ucf_enabled >= 0) {
    (void)msl_batch_set_ucf_enabled(batch, ucf_enabled != 0);
  }
  if (ucf_cardinals >= 0) {
    (void)msl_batch_set_ucf_cardinals_1_0_enabled(batch, ucf_cardinals != 0);
  }

  const size_t sample_stride = (size_t)PyArray_STRIDE(samples, 0);
  const uint8_t* samples_u8 = (const uint8_t*)PyArray_DATA(samples);
  const size_t seed_off = offsetof(MslSample, seed_t);
  const size_t prev_input_off = offsetof(MslSample, prev_input_t);
  const size_t input_off = offsetof(MslSample, input_t);
  const size_t ref_off = offsetof(MslSample, ref_t1);

  uint8_t* seed_bytes = (uint8_t*)PyMem_RawMalloc((size_t)batch_size * sizeof(MslSeed));
  uint8_t* prev_input_bytes = (uint8_t*)PyMem_RawMalloc((size_t)batch_size * sizeof(MslInput));
  uint8_t* input_bytes = (uint8_t*)PyMem_RawMalloc((size_t)batch_size * sizeof(MslInput));
  uint8_t* out_bytes = (uint8_t*)PyMem_RawMalloc((size_t)batch_size * sizeof(MslCompare));
  MslDisruptiveFirstMismatch* first =
      (MslDisruptiveFirstMismatch*)PyMem_RawMalloc((size_t)batch_size * sizeof(*first));
  if (seed_bytes == NULL || prev_input_bytes == NULL || input_bytes == NULL || out_bytes == NULL ||
      first == NULL) {
    msl_batch_destroy(batch);
    PyMem_RawFree(seed_bytes);
    PyMem_RawFree(prev_input_bytes);
    PyMem_RawFree(input_bytes);
    PyMem_RawFree(out_bytes);
    PyMem_RawFree(first);
    PyErr_NoMemory();
    return NULL;
  }

  MslDisruptiveRowVec rows = {0};
  int err = 0;
  for (int chunk_start = record_start; chunk_start < record_stop && err == 0;
       chunk_start += batch_size * stride_records) {
    int active_count = 0;
    int starts_stack[4096];
    int* starts = starts_stack;
    if (batch_size > (int)(sizeof(starts_stack) / sizeof(starts_stack[0]))) {
      starts = (int*)PyMem_RawMalloc((size_t)batch_size * sizeof(int));
      if (starts == NULL) {
        err = -100;
        break;
      }
    }
    for (int lane = 0; lane < batch_size; lane++) {
      const int rec = chunk_start + lane * stride_records;
      if (rec >= record_stop) break;
      starts[lane] = rec;
      memcpy(seed_bytes + (size_t)lane * sizeof(MslSeed),
             samples_u8 + (size_t)rec * sample_stride + seed_off, sizeof(MslSeed));
      active_count++;
    }
    if (active_count <= 0) {
      if (starts != starts_stack) PyMem_RawFree(starts);
      continue;
    }
    msl_disr_fill_inactive(seed_bytes, active_count, batch_size, sizeof(MslSeed), sizeof(MslSeed));
    if (msl_batch_reseed_seed_rollout(batch, seed_bytes, sizeof(MslSeed)) != 0) {
      err = -1;
      if (starts != starts_stack) PyMem_RawFree(starts);
      break;
    }
    memset(first, 0, (size_t)active_count * sizeof(*first));

    const size_t lane_row_offsets_cap = (size_t)active_count * (size_t)horizon_count;
    size_t* lane_row_offsets = (size_t*)PyMem_RawMalloc(lane_row_offsets_cap * sizeof(size_t));
    size_t* lane_row_counts = (size_t*)PyMem_RawCalloc((size_t)active_count, sizeof(size_t));
    if (lane_row_offsets == NULL || lane_row_counts == NULL) {
      PyMem_RawFree(lane_row_offsets);
      PyMem_RawFree(lane_row_counts);
      err = -100;
      if (starts != starts_stack) PyMem_RawFree(starts);
      break;
    }

    for (int offset = 1; offset <= max_horizon && err == 0; offset++) {
      for (int lane = 0; lane < active_count; lane++) {
        const int j = starts[lane] + offset - 1;
        memcpy(prev_input_bytes + (size_t)lane * sizeof(MslInput),
               samples_u8 + (size_t)j * sample_stride + prev_input_off, sizeof(MslInput));
        memcpy(input_bytes + (size_t)lane * sizeof(MslInput),
               samples_u8 + (size_t)j * sample_stride + input_off, sizeof(MslInput));
      }
      msl_disr_fill_inactive(prev_input_bytes, active_count, batch_size, sizeof(MslInput),
                             sizeof(MslInput));
      msl_disr_fill_inactive(input_bytes, active_count, batch_size, sizeof(MslInput),
                             sizeof(MslInput));
      if (msl_batch_step_input(batch, prev_input_bytes, sizeof(MslInput), input_bytes,
                               sizeof(MslInput)) != 0 ||
          msl_batch_write_compare(batch, out_bytes, sizeof(MslCompare)) != 0) {
        err = -2;
        break;
      }

      for (int lane = 0; lane < active_count; lane++) {
        if (!first[lane].valid) {
          const int j = starts[lane] + offset - 1;
          const MslCompare* ref =
              (const MslCompare*)(const void*)(samples_u8 + (size_t)j * sample_stride + ref_off);
          const MslCompare* out =
              (const MslCompare*)(const void*)(out_bytes + (size_t)lane * sizeof(MslCompare));
          first[lane] = msl_disr_first_mismatch(out, ref, players, player_count, discrete_fields,
                                                discrete_count, float_fields, float_count, offset,
                                                float_epsilon, profile_rl1 != 0);
        }
      }

      if (!msl_disr_horizon_contains(horizons, horizon_count, offset)) {
        continue;
      }

      for (int lane = 0; lane < active_count; lane++) {
        const int start = starts[lane];
        const int j = start + offset - 1;
        const MslSample* sample_start =
            (const MslSample*)(const void*)(samples_u8 + (size_t)start * sample_stride);
        const MslCompare* ref =
            (const MslCompare*)(const void*)(samples_u8 + (size_t)j * sample_stride + ref_off);
        const MslCompare* out =
            (const MslCompare*)(const void*)(out_bytes + (size_t)lane * sizeof(MslCompare));
        double player_scores[MSL_MAX_PLAYERS];
        double score_discrete = 0.0, score_float = 0.0, score_item = 0.0;
        msl_disr_score_row(out, ref, players, player_count, discrete_fields, discrete_count,
                           float_fields, float_count, float_epsilon, profile_rl1 != 0,
                           player_scores, &score_discrete, &score_float, &score_item);
        const double total = score_discrete + score_float + score_item;
        if (total <= 0.0) {
          continue;
        }
        const int player =
            msl_disr_selected_player(players, player_count, player_scores, score_item);
        const int action_player = player < 0 ? 0 : player;
        MslDisruptiveNativeRow row;
        memset(&row, 0, sizeof(row));
        row.record = start;
        row.seed_frame = sample_start->seed_t.frame_id;
        row.horizon = offset;
        row.ref_frame = ref->frame_id;
        row.player = player;
        const MslDisruptiveFirstMismatch fm = first[lane].valid
                                                  ? first[lane]
                                                  : (MslDisruptiveFirstMismatch){.offset = offset,
                                                                                 .player = player,
                                                                                 .field_code = 0,
                                                                                 .subindex = -1,
                                                                                 .kind = 0,
                                                                                 .valid = 0};
        row.first_offset = fm.offset;
        row.first_field_code = fm.field_code;
        row.first_subindex = fm.subindex;
        row.first_player = fm.player;
        row.first_kind = fm.kind;
        row.first_out_i = fm.out_i;
        row.first_ref_i = fm.ref_i;
        row.first_out_f = fm.out_f;
        row.first_ref_f = fm.ref_f;
        row.score_total = total;
        row.score_discrete = score_discrete;
        row.score_float = score_float;
        row.score_item = score_item;
        row.seed_action_id = sample_start->seed_t.action_id[action_player];
        row.out_action_id = out->action_id[action_player];
        row.ref_action_id = ref->action_id[action_player];
        row.seed_action_frame = sample_start->seed_t.action_frame[action_player];
        row.out_action_frame = out->action_frame[action_player];
        row.ref_action_frame = ref->action_frame[action_player];
        row.on_ground = ref->on_ground[action_player];
        row.hitlag = ref->hitlag[action_player];
        row.hitstun = ref->hitstun[action_player];
        if (player >= 0) {
          row.family_field_mask = msl_disr_mismatched_player_mask(
              out, ref, player, discrete_fields, discrete_count, float_fields, float_count,
              float_epsilon, profile_rl1 != 0);
        } else if (fm.field_code != 0) {
          row.family_field_mask = msl_disr_mask_for_field(fm.field_code, fm.subindex);
        }
        if (msl_disr_vec_push(&rows, &row) != 0) {
          err = -100;
          break;
        }
        lane_row_offsets[(size_t)lane * (size_t)horizon_count + lane_row_counts[lane]++] =
            rows.len - 1u;
      }
    }

    if (err == 0) {
      MslDisruptiveNativeRow* ordered =
          (MslDisruptiveNativeRow*)PyMem_RawMalloc(rows.len * sizeof(*ordered));
      if (ordered == NULL && rows.len > 0) {
        err = -100;
      } else {
        size_t before = rows.len;
        size_t chunk_total = 0;
        for (int lane = 0; lane < active_count; lane++) {
          chunk_total += lane_row_counts[lane];
        }
        size_t prefix = before - chunk_total;
        if (prefix > 0) {
          memcpy(ordered, rows.data, prefix * sizeof(*ordered));
        }
        size_t pos = prefix;
        for (int lane = 0; lane < active_count; lane++) {
          for (size_t k = 0; k < lane_row_counts[lane]; k++) {
            const size_t idx = lane_row_offsets[(size_t)lane * (size_t)horizon_count + k];
            ordered[pos++] = rows.data[idx];
          }
        }
        memcpy(rows.data, ordered, before * sizeof(*ordered));
        PyMem_RawFree(ordered);
      }
    }
    PyMem_RawFree(lane_row_offsets);
    PyMem_RawFree(lane_row_counts);
    if (starts != starts_stack) PyMem_RawFree(starts);
  }

  msl_batch_destroy(batch);
  PyMem_RawFree(seed_bytes);
  PyMem_RawFree(prev_input_bytes);
  PyMem_RawFree(input_bytes);
  PyMem_RawFree(out_bytes);
  PyMem_RawFree(first);

  if (err != 0) {
    PyMem_RawFree(rows.data);
    if (err == -100) {
      PyErr_NoMemory();
    } else {
      PyErr_Format(PyExc_RuntimeError, "native disruptive scan failed: %d", err);
    }
    return NULL;
  }

  npy_intp int_dims[2] = {(npy_intp)rows.len, 22};
  npy_intp float_dims[2] = {(npy_intp)rows.len, 6};
  PyObject* ints_obj = PyArray_SimpleNew(2, int_dims, NPY_INT64);
  PyObject* floats_obj = PyArray_SimpleNew(2, float_dims, NPY_FLOAT64);
  if (ints_obj == NULL || floats_obj == NULL) {
    Py_XDECREF(ints_obj);
    Py_XDECREF(floats_obj);
    PyMem_RawFree(rows.data);
    return NULL;
  }
  int64_t* ints = (int64_t*)PyArray_DATA((PyArrayObject*)ints_obj);
  double* floats = (double*)PyArray_DATA((PyArrayObject*)floats_obj);
  for (size_t i = 0; i < rows.len; i++) {
    const MslDisruptiveNativeRow* r = &rows.data[i];
    int64_t* iv = ints + i * 22u;
    double* fv = floats + i * 6u;
    iv[0] = r->record;
    iv[1] = r->seed_frame;
    iv[2] = r->horizon;
    iv[3] = r->ref_frame;
    iv[4] = r->player;
    iv[5] = r->first_offset;
    iv[6] = r->first_field_code;
    iv[7] = r->first_subindex;
    iv[8] = r->first_player;
    iv[9] = r->first_kind;
    iv[10] = r->first_out_i;
    iv[11] = r->first_ref_i;
    iv[12] = r->seed_action_id;
    iv[13] = r->out_action_id;
    iv[14] = r->ref_action_id;
    iv[15] = r->seed_action_frame;
    iv[16] = r->out_action_frame;
    iv[17] = r->ref_action_frame;
    iv[18] = r->on_ground;
    iv[19] = r->hitlag;
    iv[20] = r->hitstun;
    iv[21] = (int64_t)r->family_field_mask;
    fv[0] = r->first_out_f;
    fv[1] = r->first_ref_f;
    fv[2] = r->score_total;
    fv[3] = r->score_discrete;
    fv[4] = r->score_float;
    fv[5] = r->score_item;
  }
  PyMem_RawFree(rows.data);

  PyObject* ret = Py_BuildValue("(NN)", ints_obj, floats_obj);
  return ret;
}

static PyObject* msl_write_gamestate(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* viewpoint_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &viewpoint_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
  if (viewpoint == NULL) {
    return NULL;
  }
  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(viewpoint, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "viewpoint has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(viewpoint, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "out has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(out, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MeleeGamestate)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MeleeGamestate");
    return NULL;
  }

  const uint8_t* viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
  const size_t viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);
  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t out_stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = melee_batch_write_gamestate(h->batch, viewpoint_bytes, viewpoint_stride,
                                              out_bytes, out_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_write_terminal(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "OO|i", &handle_obj, &out_obj, &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(out, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "out has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(out, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslTerminal)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslTerminal");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t out_stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_terminal(h->batch, out_bytes, out_stride, (int32_t)max_frame_id);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_terminal failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_timebase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  if (!PyArg_ParseTuple(args, "Oi", &handle_obj, &batch_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)MSL_MAX_PLAYERS, (npy_intp)8};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = msl_batch_debug_timebase(h->batch, batch_index, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_timebase failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_hitbox_event_timing_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &attacker, &hb_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)1, (npy_intp)sizeof(MslDebugHitboxEventTiming)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  MslDebugHitboxEventTiming* out = (MslDebugHitboxEventTiming*)PyArray_DATA(arr);
  const int err = msl_batch_debug_hitbox_event_timing(h->batch, batch_index, attacker, hb_id, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitbox_event_timing failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_hitbox_sweep_proxy_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &attacker, &hb_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)1, (npy_intp)sizeof(MslDebugHitboxSweepProxy)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  MslDebugHitboxSweepProxy* out = (MslDebugHitboxSweepProxy*)PyArray_DATA(arr);
  const int err = msl_batch_debug_hitbox_sweep_proxy(h->batch, batch_index, attacker, hb_id, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitbox_sweep_proxy failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_hurtcap_slot_flags_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int cap_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &player_index, &cap_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)1, (npy_intp)sizeof(MslDebugHurtcapSlotFlags)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  MslDebugHurtcapSlotFlags* out = (MslDebugHurtcapSlotFlags*)PyArray_DATA(arr);
  const int err =
      msl_batch_debug_hurtcap_slot_flags(h->batch, batch_index, player_index, cap_id, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hurtcap_slot_flags failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_hurtcap_geometry_valid_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  uint8_t valid = 0u;
  const int err =
      msl_batch_debug_hurtcap_geometry_valid(h->batch, batch_index, player_index, &valid);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hurtcap_geometry_valid failed: %d", err);
    return NULL;
  }
  return PyLong_FromLong((long)valid);
}

static PyObject* msl_debug_hurtcap_matrix_valid_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int cap_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &player_index, &cap_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  uint8_t valid = 0u;
  const int err =
      msl_batch_debug_hurtcap_matrix_valid(h->batch, batch_index, player_index, cap_id, &valid);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hurtcap_matrix_valid failed: %d", err);
    return NULL;
  }
  return PyLong_FromLong((long)valid);
}

static PyObject* msl_debug_poison_hurtcap_matrix_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int cap_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &player_index, &cap_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  const int err =
      msl_batch_debug_poison_hurtcap_matrix(h->batch, batch_index, player_index, cap_id);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_poison_hurtcap_matrix failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_dynamic_pose_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)1, (npy_intp)sizeof(MslDebugDynamicPoseState)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  MslDebugDynamicPoseState* out = (MslDebugDynamicPoseState*)PyArray_DATA(arr);
  const int err = msl_batch_debug_dynamic_pose_state(h->batch, batch_index, player_index, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_dynamic_pose_state failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_get_fighter_8006cda4_pre_gate_consume_count_py(PyObject* self,
                                                                          PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  uint8_t count = 0u;
  const int err = msl_batch_debug_get_fighter_8006cda4_pre_gate_consume_count(h->batch, batch_index,
                                                                              player_index, &count);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError,
                 "msl_batch_debug_get_fighter_8006cda4_pre_gate_consume_count failed: %d", err);
    return NULL;
  }
  return PyLong_FromUnsignedLong((unsigned long)count);
}

static PyObject* msl_debug_attackairb_continuation_overlap_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  int defender = 0;
  int cap_id = 0;
  if (!PyArg_ParseTuple(args, "Oiiiii", &handle_obj, &batch_index, &attacker, &hb_id, &defender,
                        &cap_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  float overlap = 0.0f;
  const int err = msl_batch_debug_attackairb_continuation_overlap(
      h->batch, batch_index, attacker, hb_id, defender, cap_id, &overlap);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_attackairb_continuation_overlap failed: %d",
                 err);
    return NULL;
  }
  return PyFloat_FromDouble((double)overlap);
}

static PyObject* msl_debug_body_matrix_overlap_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  int defender = 0;
  int cap_id = 0;
  if (!PyArg_ParseTuple(args, "Oiiiii", &handle_obj, &batch_index, &attacker, &hb_id, &defender,
                        &cap_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  float overlap = 0.0f;
  const int err = msl_batch_debug_body_matrix_overlap(h->batch, batch_index, attacker, hb_id,
                                                      defender, cap_id, &overlap);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_body_matrix_overlap failed: %d", err);
    return NULL;
  }
  return PyFloat_FromDouble((double)overlap);
}

static PyObject* msl_debug_write_processed_input(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslProcessedInput)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslProcessedInput");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_processed_input(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_processed_input failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_write_stage_state(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslDebugStageState)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslDebugStageState");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_stage_state(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_stage_state failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_write_internals(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslDebugInternals)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslDebugInternals");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_internals(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_internals failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_write_collision_contacts(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslDebugCollisionContacts)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslDebugCollisionContacts");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_collision_contacts(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_collision_contacts failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_write_colldata_ecb(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslDebugCollDataEcb)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslDebugCollDataEcb");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_colldata_ecb(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_colldata_ecb failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_force_anim_timebase_enter(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  double anim_start = 0.0;
  double anim_speed = 0.0;
  if (!PyArg_ParseTuple(args, "Oiidd", &handle_obj, &batch_index, &player_index, &anim_start,
                        &anim_speed)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_force_anim_timebase_enter(h->batch, batch_index, player_index,
                                                            (float)anim_start, (float)anim_speed);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_force_anim_timebase_enter failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_sizes(PyObject* self, PyObject* args) {
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

static PyObject* msl_data_schema_versions(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  return Py_BuildValue("{s:I,s:I}", "attack_id_move_id", attack_id_tables_format_version(),
                       "motion_state_owners", motion_state_owners_format_version());
}

static PyObject* msl_alloc_reset(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  msl_alloc_reset_counters();
  Py_RETURN_NONE;
}

static PyObject* msl_alloc_stats(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  const unsigned long long calls = (unsigned long long)msl_alloc_total_calls();
  const unsigned long long bytes = (unsigned long long)msl_alloc_total_bytes();
  return Py_BuildValue("{s:K,s:K}", "calls", calls, "bytes", bytes);
}

static PyObject* msl_char_params_ecb_joints_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_char_params_part_anchors_py(PyObject* self, PyObject* args) {
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
      "{s:N,s:i,s:i,s:i,s:i}", "ecb_joints", ecb, "laser_spawn_joint_part_id",
      (int)ch->laser_spawn_joint_part_id, "reflector_bone_part_id", (int)ch->reflector_bone_part_id,
      "camera_zoom_target_bone_part_id", (int)ch->camera_zoom_target_bone_part_id,
      "grab_capture_anchor_part_id", (int)ch->grab_capture_anchor_part_id);
  return out;
}

static PyObject* msl_item_article_params_py(PyObject* self, PyObject* args) {
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
  return Py_BuildValue(
      "{s:i,s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:f}", "blaster_shot_itkind",
      (int)p->blaster_shot_itkind, "blaster_gun_itkind", (int)p->blaster_gun_itkind,
      "laser_spawn_joint_part_id", (int)p->laser_spawn_joint_part_id, "laser_lifetime_frames",
      (int)p->laser_lifetime_frames, "side_special_illusion_itkind",
      (int)p->side_special_illusion_itkind, "laser_damage", (double)p->laser_damage, "laser_size",
      (double)p->laser_size, "illusion_item_state0_damage", (double)p->illusion_item_state0_damage,
      "illusion_item_state1_damage", (double)p->illusion_item_state1_damage,
      "shield_bounce_extra_degrees", (double)p->shield_bounce_extra_degrees);
}

static PyObject* msl_stage_floor_segment_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_debug_stage_moving_floor_surface_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_topology_flags_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_fighter_floor_segment_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_ceiling_segment_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_wall_segment_py(PyObject* self, PyObject* args, uint8_t left_wall) {
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

static PyObject* msl_stage_left_wall_segment_py(PyObject* self, PyObject* args) {
  return msl_stage_wall_segment_py(self, args, 1u);
}

static PyObject* msl_stage_right_wall_segment_py(PyObject* self, PyObject* args) {
  return msl_stage_wall_segment_py(self, args, 0u);
}

static PyObject* msl_stage_raw_line_non_kind_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_static_query_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_mpcoll_check_bounding_aabb_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_mpcoll_end_publication_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_stage_match_flow_roles_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_debug_reset_pose_and_hitboxes_tables_py(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  const int err = msl_debug_reset_pose_and_hitboxes_tables();
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_debug_reset_pose_and_hitboxes_tables failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_ecb_bottom_rel_y_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned long anim_u = 0;
  int action_frame = 0;
  if (!PyArg_ParseTuple(args, "Iki", &char_id_u, &anim_u, &action_frame)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  // Test/tool helpers call these accessors without creating a batch. Ensure ECB tables are loaded.
  // (ecb_table_init/ecb_extents_table_init are idempotent.)
  if (ecb_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ecb_table_init failed (missing/invalid data/ecb/*_bottom.bin)");
    return NULL;
  }
  const float y = msl_ecb_bottom_rel_y((uint8_t)char_id_u, (uint32_t)anim_u, action_frame);
  return PyFloat_FromDouble((double)y);
}

static PyObject* msl_ecb_extents_rel_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned long anim_u = 0;
  int action_frame = 0;
  if (!PyArg_ParseTuple(args, "Iki", &char_id_u, &anim_u, &action_frame)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  // Test/tool helpers call these accessors without creating a batch. Ensure ECB tables are loaded.
  // (ecb_table_init/ecb_extents_table_init are idempotent.)
  if (ecb_extents_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ecb_extents_table_init failed (missing/invalid data/ecb/*_extents.bin)");
    return NULL;
  }
  const MslEcbExtentsRel ex =
      msl_ecb_extents_rel((uint8_t)char_id_u, (uint32_t)anim_u, action_frame);
  return Py_BuildValue("(ffff)", ex.min_x, ex.max_x, ex.min_y, ex.max_y);
}

static PyObject* msl_anim_pose_matrix_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned int msid_u = 0;
  unsigned int frame_u = 0;
  unsigned int part_id_u = 0;
  if (!PyArg_ParseTuple(args, "IIII", &char_id_u, &msid_u, &frame_u, &part_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  if (msid_u > 0xFFFFu || frame_u > 0xFFFFu || part_id_u > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "msid/frame/part_id out of range");
    return NULL;
  }

  npy_intp dims[1] = {(npy_intp)12};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = anim_pose_get_matrix((uint8_t)char_id_u, (uint16_t)msid_u, (uint16_t)frame_u,
                                       (uint16_t)part_id_u, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_SetString(PyExc_ValueError, "anim_pose_get_matrix failed");
    return NULL;
  }
  return (PyObject*)arr;
}

static PyObject* msl_hurtcaps_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)MSL_MAX_HURTCAPS, (npy_intp)7};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  uint8_t count = 0;
  const int err = msl_batch_debug_hurtcaps_world(h->batch, batch_index, player_index, out, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hurtcaps_world failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_hitboxes_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)MSL_MAX_HITBOXES, (npy_intp)10};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  uint8_t count = 0;
  const int err = msl_batch_debug_hitboxes_world(h->batch, batch_index, player_index, out, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitboxes_world failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_hitboxes_world_full_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)MSL_MAX_HITBOXES, (npy_intp)16};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  uint8_t count = 0;
  const int err =
      msl_batch_debug_hitboxes_world_full(h->batch, batch_index, player_index, out, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitboxes_world_full failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_combat_contacts_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_contacts = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_contacts)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_contacts < 0 || max_contacts > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_contacts out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_contacts, (npy_intp)sizeof(MslDebugCombatContact)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugCombatContact* out = (MslDebugCombatContact*)PyArray_DATA(arr);
  const int err =
      msl_batch_debug_combat_contacts(h->batch, batch_index, out, (uint16_t)max_contacts, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_combat_contacts failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_combat_contacts_filtered_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_contacts = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_contacts)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_contacts < 0 || max_contacts > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_contacts out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_contacts, (npy_intp)sizeof(MslDebugCombatContact)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugCombatContact* out = (MslDebugCombatContact*)PyArray_DATA(arr);
  const int err = msl_batch_debug_combat_contacts_filtered(h->batch, batch_index, out,
                                                           (uint16_t)max_contacts, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_combat_contacts_filtered failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_combat_select_body_hits_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_contacts = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_contacts)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_contacts < 0 || max_contacts > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_contacts out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_contacts, (npy_intp)sizeof(MslDebugCombatContact)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugCombatContact* out = (MslDebugCombatContact*)PyArray_DATA(arr);
  const int err = msl_batch_debug_combat_select_body_hits(h->batch, batch_index, out,
                                                          (uint16_t)max_contacts, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_combat_select_body_hits failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_combat_contacts_classified_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_contacts = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_contacts)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_contacts < 0 || max_contacts > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_contacts out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_contacts, (npy_intp)sizeof(MslDebugCombatContactClassified)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugCombatContactClassified* out = (MslDebugCombatContactClassified*)PyArray_DATA(arr);
  const int err = msl_batch_debug_combat_contacts_classified(h->batch, batch_index, out,
                                                             (uint16_t)max_contacts, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_combat_contacts_classified failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_combat_contacts_classified_filtered_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_contacts = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_contacts)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_contacts < 0 || max_contacts > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_contacts out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_contacts, (npy_intp)sizeof(MslDebugCombatContactClassified)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugCombatContactClassified* out = (MslDebugCombatContactClassified*)PyArray_DATA(arr);
  const int err = msl_batch_debug_combat_contacts_classified_filtered(
      h->batch, batch_index, out, (uint16_t)max_contacts, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_combat_contacts_classified_filtered failed: %d",
                 err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_shield_candidate_decisions_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int max_rows = 256;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &batch_index, &max_rows)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (max_rows < 0 || max_rows > 0xFFFF) {
    PyErr_SetString(PyExc_ValueError, "max_rows out of range");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)max_rows, (npy_intp)sizeof(MslDebugShieldCandidateDecision)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  uint16_t count = 0;
  MslDebugShieldCandidateDecision* out = (MslDebugShieldCandidateDecision*)PyArray_DATA(arr);
  const int err = msl_batch_debug_shield_candidate_decisions(h->batch, batch_index, out,
                                                             (uint16_t)max_rows, &count);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_shield_candidate_decisions failed: %d", err);
    return NULL;
  }

  return Py_BuildValue("(Oi)", arr, (int)count);
}

static PyObject* msl_debug_shield_bubbles_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  if (!PyArg_ParseTuple(args, "Oi", &handle_obj, &batch_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)MSL_MAX_PLAYERS, (npy_intp)4};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = msl_batch_debug_shield_bubbles_world(h->batch, batch_index, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_shield_bubbles_world failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_debug_clear_hitboxes_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_clear_hitboxes_world(h->batch, batch_index, player_index);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_clear_hitboxes_world failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f, damage = 0.0f;
  int enabled = 1;
  if (!PyArg_ParseTuple(args, "Oiiifffff|i", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &x, &y, &z, &radius, &damage, &enabled)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_world(h->batch, batch_index, player_index, hitbox_id,
                                                   x, y, z, radius, damage, enabled);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_world failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_flags_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  unsigned int hitbox_flags = 0;
  if (!PyArg_ParseTuple(args, "OiiiI", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &hitbox_flags)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (hitbox_flags > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "hitbox_flags out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_flags(h->batch, batch_index, player_index, hitbox_id,
                                                   (uint16_t)hitbox_flags);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_flags failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_group_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  unsigned int hit_group = 0;
  if (!PyArg_ParseTuple(args, "OiiiI", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &hit_group)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (hit_group > 7u) {
    PyErr_SetString(PyExc_ValueError, "hit_group out of range (expected 0..7)");
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_group(h->batch, batch_index, player_index, hitbox_id,
                                                   (uint8_t)hit_group);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_group failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_enable_edge_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  unsigned int enable_edge = 0;
  if (!PyArg_ParseTuple(args, "OiiiI", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &enable_edge)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (enable_edge > 1u) {
    PyErr_SetString(PyExc_ValueError, "enable_edge out of range (expected 0 or 1)");
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_enable_edge(h->batch, batch_index, player_index,
                                                         hitbox_id, (uint8_t)enable_edge);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_enable_edge failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_element_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  unsigned int element = 0;
  if (!PyArg_ParseTuple(args, "OiiiI", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &element)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (element > 0xFFu) {
    PyErr_SetString(PyExc_ValueError, "element out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_element(h->batch, batch_index, player_index, hitbox_id,
                                                     (uint8_t)element);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_element failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitbox_kb_params_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hitbox_id = 0;
  unsigned int angle_deg = 0;
  unsigned int kbg = 0;
  unsigned int wsk = 0;
  unsigned int bkb = 0;
  if (!PyArg_ParseTuple(args, "OiiiIIII", &handle_obj, &batch_index, &player_index, &hitbox_id,
                        &angle_deg, &kbg, &wsk, &bkb)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (angle_deg > 0xFFFFu || kbg > 0xFFFFu || wsk > 0xFFFFu || bkb > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "hitbox kb params out of range (expected u16)");
    return NULL;
  }
  const int err = msl_batch_debug_set_hitbox_kb_params(h->batch, batch_index, player_index,
                                                       hitbox_id, (uint16_t)angle_deg,
                                                       (uint16_t)kbg, (uint16_t)wsk, (uint16_t)bkb);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitbox_kb_params failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_rollout_clock_mode_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int mode = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &mode)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (mode < 0 || mode > 255) {
    PyErr_SetString(PyExc_ValueError, "rollout clock mode out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_rollout_clock_mode(h->batch, batch_index, (uint8_t)mode);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_rollout_clock_mode failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_get_rollout_clock_mode_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  if (!PyArg_ParseTuple(args, "Oi", &handle_obj, &batch_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  uint8_t mode = 0u;
  const int err = msl_batch_debug_get_rollout_clock_mode(h->batch, batch_index, &mode);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_get_rollout_clock_mode failed: %d", err);
    return NULL;
  }
  return PyLong_FromLong((long)mode);
}

static PyObject* msl_debug_set_camera_mode_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int mode = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &mode)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (mode < 0 || mode > 255) {
    PyErr_SetString(PyExc_ValueError, "camera mode out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_camera_mode(h->batch, batch_index, (uint8_t)mode);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_camera_mode failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_clear_hurtcaps_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "Oii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_clear_hurtcaps_world(h->batch, batch_index, player_index);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_clear_hurtcaps_world failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hurtcap_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hurtcap_id = 0;
  float ax = 0.0f, ay = 0.0f, az = 0.0f, bx = 0.0f, by = 0.0f, bz = 0.0f, radius = 0.0f;
  if (!PyArg_ParseTuple(args, "Oiiifffffff", &handle_obj, &batch_index, &player_index, &hurtcap_id,
                        &ax, &ay, &az, &bx, &by, &bz, &radius)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_hurtcap_world(h->batch, batch_index, player_index, hurtcap_id,
                                                    ax, ay, az, bx, by, bz, radius);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hurtcap_world failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hurtcap_height_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hurtcap_id = 0;
  unsigned int height = 0;
  if (!PyArg_ParseTuple(args, "OiiiI", &handle_obj, &batch_index, &player_index, &hurtcap_id,
                        &height)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (height > 0xFFu) {
    PyErr_SetString(PyExc_ValueError, "height out of range (expected u8)");
    return NULL;
  }
  const int err = msl_batch_debug_set_hurtcap_height(h->batch, batch_index, player_index,
                                                     hurtcap_id, (uint8_t)height);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hurtcap_height failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hurtcap_enabled_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int hurtcap_id = 0;
  int enabled = 0;
  if (!PyArg_ParseTuple(args, "Oiiii", &handle_obj, &batch_index, &player_index, &hurtcap_id,
                        &enabled)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err =
      msl_batch_debug_set_hurtcap_enabled(h->batch, batch_index, player_index, hurtcap_id, enabled);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hurtcap_enabled failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_combat_resolve_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_combat_resolve(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_combat_resolve failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hitlag_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int hitlag_frames = 0;
  if (!PyArg_ParseTuple(args, "OiiI", &handle_obj, &batch_index, &player_index, &hitlag_frames)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (hitlag_frames > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "hitlag_frames out of range");
    return NULL;
  }
  const int err =
      msl_batch_debug_set_hitlag(h->batch, batch_index, player_index, (uint16_t)hitlag_frames);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hitlag failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_damage_source_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int last_hit_by = 0;
  unsigned int instance_hit_by = 0;
  if (!PyArg_ParseTuple(args, "OiiII", &handle_obj, &batch_index, &player_index, &last_hit_by,
                        &instance_hit_by)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (last_hit_by > 0xFFu || instance_hit_by > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "damage source values out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_damage_source(
      h->batch, batch_index, player_index, (uint8_t)last_hit_by, (uint16_t)instance_hit_by);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_damage_source failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_damage_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int action_id = 0;
  unsigned int hitstun = 0;
  int damage_time_since_hit = 0;
  unsigned int on_ground = 0;
  if (!PyArg_ParseTuple(args, "OiiIIiI", &handle_obj, &batch_index, &player_index, &action_id,
                        &hitstun, &damage_time_since_hit, &on_ground)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (action_id > 0xFFFFu || hitstun > 0xFFFFu || damage_time_since_hit < -32768 ||
      damage_time_since_hit > 32767 || on_ground > 1u) {
    PyErr_SetString(PyExc_ValueError, "damage phase values out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_damage_phase(
      h->batch, batch_index, player_index, (uint16_t)action_id, (uint16_t)hitstun,
      (int16_t)damage_time_since_hit, (uint8_t)on_ground);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_damage_phase failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_phantom_damage_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  float pending_damage = 0.0f;
  unsigned int timer = 0;
  unsigned int source_slot = 0;
  if (!PyArg_ParseTuple(args, "OiifII", &handle_obj, &batch_index, &player_index, &pending_damage,
                        &timer, &source_slot)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (timer > 0xFFFFu || source_slot > 0xFFu) {
    PyErr_SetString(PyExc_ValueError, "phantom damage values out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_phantom_damage(
      h->batch, batch_index, player_index, pending_damage, (uint16_t)timer, (uint8_t)source_slot);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_phantom_damage failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_prev_action_id_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int prev_action_id = 0;
  if (!PyArg_ParseTuple(args, "OiiI", &handle_obj, &batch_index, &player_index, &prev_action_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (prev_action_id > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "prev_action_id out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_prev_action_id(h->batch, batch_index, player_index,
                                                     (uint16_t)prev_action_id);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_prev_action_id failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_smash_charge_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int state = 0;
  unsigned int frames = 0;
  unsigned int hold_frames_max = 0;
  if (!PyArg_ParseTuple(args, "OiiIII", &handle_obj, &batch_index, &player_index, &state, &frames,
                        &hold_frames_max)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (state > 0xFFu || frames > 0xFFu || hold_frames_max > 0xFFu) {
    PyErr_SetString(PyExc_ValueError, "smash charge values out of range");
    return NULL;
  }
  const int err =
      msl_batch_debug_set_smash_charge_state(h->batch, batch_index, player_index, (uint8_t)state,
                                             (uint8_t)frames, (uint8_t)hold_frames_max);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_smash_charge_state failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_set_hit_status_override_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int status = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &player_index, &status)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (status < -1 || status > 0xFF) {
    PyErr_SetString(PyExc_ValueError, "status out of range (expected -1..255)");
    return NULL;
  }
  const int err =
      msl_batch_debug_set_hit_status_override(h->batch, batch_index, player_index, status);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_hit_status_override failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* msl_debug_point_segment_dist2_py(PyObject* self, PyObject* args) {
  (void)self;
  float px = 0.0f, py = 0.0f, pz = 0.0f;
  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  if (!PyArg_ParseTuple(args, "fffffffff", &px, &py, &pz, &ax, &ay, &az, &bx, &by, &bz)) {
    return NULL;
  }
  float d2 = 0.0f;
  float t = 0.0f;
  const int err = msl_debug_point_segment_dist2(px, py, pz, ax, ay, az, bx, by, bz, &d2, &t);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_debug_point_segment_dist2 failed: %d", err);
    return NULL;
  }
  return Py_BuildValue("(ff)", d2, t);
}

static PyObject* msl_pose_points_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* matrices_obj = NULL;
  PyObject* local_xyz_obj = NULL;
  PyObject* out_xyz_obj = NULL;
  int count = 0;
  float model_scale = 1.0f;
  float facing_dir = 1.0f;
  float px = 0.0f;
  float py = 0.0f;
  float pz = 0.0f;
  if (!PyArg_ParseTuple(args, "OOifffffO", &matrices_obj, &local_xyz_obj, &count, &model_scale,
                        &facing_dir, &px, &py, &pz, &out_xyz_obj)) {
    return NULL;
  }
  PyArrayObject* matrices = require_contiguous_array(matrices_obj, NPY_FLOAT32, 2, "matrices");
  if (matrices == NULL) {
    return NULL;
  }
  PyArrayObject* local_xyz = require_contiguous_array(local_xyz_obj, NPY_FLOAT32, 2, "local_xyz");
  if (local_xyz == NULL) {
    return NULL;
  }
  PyArrayObject* out_xyz = require_contiguous_array(out_xyz_obj, NPY_FLOAT32, 2, "out_xyz");
  if (out_xyz == NULL) {
    return NULL;
  }
  if (count < 0) {
    PyErr_SetString(PyExc_ValueError, "count must be >= 0");
    return NULL;
  }
  if (PyArray_DIM(matrices, 1) != 12 || PyArray_DIM(local_xyz, 1) != 3 ||
      PyArray_DIM(out_xyz, 1) != 3) {
    PyErr_SetString(PyExc_ValueError, "expected matrices[:,12], local_xyz[:,3], out_xyz[:,3]");
    return NULL;
  }
  if ((npy_intp)count > PyArray_DIM(matrices, 0) || (npy_intp)count > PyArray_DIM(local_xyz, 0) ||
      (npy_intp)count > PyArray_DIM(out_xyz, 0)) {
    PyErr_SetString(PyExc_ValueError, "count exceeds array length");
    return NULL;
  }
  const float* matrices_p = (const float*)PyArray_DATA(matrices);
  const float* local_p = (const float*)PyArray_DATA(local_xyz);
  float* out_p = (float*)PyArray_DATA(out_xyz);
  for (int i = 0; i < count; i++) {
    const float* m = matrices_p + (size_t)i * 12u;
    const float* v = local_p + (size_t)i * 3u;
    float* out = out_p + (size_t)i * 3u;
    const float x = v[0];
    const float y = v[1];
    const float z = v[2];
    float lx = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
    float ly = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
    float lz = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
    lx = (float)(lx * model_scale);
    ly = (float)(ly * model_scale);
    lz = (float)(lz * model_scale);
    out[0] = (float)(facing_dir * lz + px);
    out[1] = (float)(ly + py);
    out[2] = (float)(-facing_dir * lx + pz);
  }
  Py_RETURN_NONE;
}

static PyObject* msl_anim_bake_ssanim01_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* rest_rot_obj = NULL;
  PyObject* rest_pos_obj = NULL;
  PyObject* rest_scl_obj = NULL;
  PyObject* parent_part_obj = NULL;
  PyObject* part_flags_obj = NULL;
  PyObject* order_obj = NULL;
  PyObject* local_parts_obj = NULL;
  PyObject* joint_parts_obj = NULL;
  PyObject* update_parts_obj = NULL;
  PyObject* fobj_starts_obj = NULL;
  PyObject* fobj_desc_obj = NULL;
  PyObject* ad_source_obj = NULL;
  int frame_count = 0;
  int inv_scale_part = -1;
  float inv_model_scale = 1.0f;
  float end_frame = 0.0f;
  int aobj_loop = 0;

  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOiiffi", &rest_rot_obj, &rest_pos_obj, &rest_scl_obj,
                        &parent_part_obj, &part_flags_obj, &order_obj, &local_parts_obj,
                        &joint_parts_obj, &update_parts_obj, &fobj_starts_obj, &fobj_desc_obj,
                        &ad_source_obj, &frame_count, &inv_scale_part, &inv_model_scale, &end_frame,
                        &aobj_loop)) {
    return NULL;
  }

  PyArrayObject* rest_rot = require_contiguous_array(rest_rot_obj, NPY_FLOAT32, 2, "rest_rot");
  if (rest_rot == NULL) {
    return NULL;
  }
  PyArrayObject* rest_pos = require_contiguous_array(rest_pos_obj, NPY_FLOAT32, 2, "rest_pos");
  if (rest_pos == NULL) {
    return NULL;
  }
  PyArrayObject* rest_scl = require_contiguous_array(rest_scl_obj, NPY_FLOAT32, 2, "rest_scl");
  if (rest_scl == NULL) {
    return NULL;
  }
  PyArrayObject* parent_part =
      require_contiguous_array(parent_part_obj, NPY_INT16, 1, "parent_part");
  if (parent_part == NULL) {
    return NULL;
  }
  PyArrayObject* part_flags = require_contiguous_array(part_flags_obj, NPY_UINT32, 1, "part_flags");
  if (part_flags == NULL) {
    return NULL;
  }
  PyArrayObject* order = require_contiguous_array(order_obj, NPY_INT32, 1, "order");
  if (order == NULL) {
    return NULL;
  }
  PyArrayObject* local_parts =
      require_contiguous_array(local_parts_obj, NPY_INT32, 1, "local_parts");
  if (local_parts == NULL) {
    return NULL;
  }
  PyArrayObject* joint_parts =
      require_contiguous_array(joint_parts_obj, NPY_INT32, 1, "joint_parts");
  if (joint_parts == NULL) {
    return NULL;
  }
  PyArrayObject* update_parts =
      require_contiguous_array(update_parts_obj, NPY_INT32, 1, "update_parts");
  if (update_parts == NULL) {
    return NULL;
  }
  PyArrayObject* fobj_starts =
      require_contiguous_array(fobj_starts_obj, NPY_INT32, 1, "fobj_starts");
  if (fobj_starts == NULL) {
    return NULL;
  }
  PyArrayObject* fobj_desc = require_contiguous_array(fobj_desc_obj, NPY_UINT32, 2, "fobj_desc");
  if (fobj_desc == NULL) {
    return NULL;
  }
  if (!PyBytes_Check(ad_source_obj)) {
    PyErr_SetString(PyExc_TypeError, "ad_source must be bytes");
    return NULL;
  }

  if (frame_count < 0) {
    PyErr_SetString(PyExc_ValueError, "frame_count must be >= 0");
    return NULL;
  }

  const npy_intp parts_num = PyArray_DIM(rest_rot, 0);
  if (PyArray_NDIM(rest_rot) != 2 || PyArray_DIM(rest_rot, 1) != 3 || PyArray_NDIM(rest_pos) != 2 ||
      PyArray_DIM(rest_pos, 1) != 3 || PyArray_NDIM(rest_scl) != 2 ||
      PyArray_DIM(rest_scl, 1) != 3) {
    PyErr_SetString(PyExc_ValueError, "rest_rot/rest_pos/rest_scl must have shape (parts, 3)");
    return NULL;
  }
  if (PyArray_DIM(rest_pos, 0) != parts_num || PyArray_DIM(rest_scl, 0) != parts_num ||
      PyArray_DIM(parent_part, 0) != parts_num || PyArray_DIM(part_flags, 0) != parts_num) {
    PyErr_SetString(PyExc_ValueError,
                    "rest arrays and parent_part/part_flags must share parts dim");
    return NULL;
  }
  if (parts_num <= 0) {
    PyErr_SetString(PyExc_ValueError, "parts_num must be > 0");
    return NULL;
  }

  if (PyArray_NDIM(fobj_desc) != 2 || PyArray_DIM(fobj_desc, 1) != 3) {
    PyErr_SetString(PyExc_ValueError, "fobj_desc must have shape (num_fobjs, 3) uint32");
    return NULL;
  }
  const npy_intp num_fobjs = PyArray_DIM(fobj_desc, 0);
  const npy_intp update_parts_n = PyArray_DIM(update_parts, 0);
  if (PyArray_DIM(fobj_starts, 0) != update_parts_n + 1) {
    PyErr_SetString(PyExc_ValueError, "fobj_starts must have len(update_parts)+1");
    return NULL;
  }

  const int32_t* fobj_starts_ptr = (const int32_t*)PyArray_DATA(fobj_starts);
  if (update_parts_n > 0) {
    const int32_t last = fobj_starts_ptr[update_parts_n];
    if (last < 0 || (npy_intp)last != num_fobjs) {
      PyErr_SetString(PyExc_ValueError, "fobj_starts[-1] must equal num_fobjs");
      return NULL;
    }
  } else {
    if (num_fobjs != 0) {
      PyErr_SetString(PyExc_ValueError, "num_fobjs must be 0 when update_parts is empty");
      return NULL;
    }
  }

  const char* ad_base = PyBytes_AS_STRING(ad_source_obj);
  const Py_ssize_t ad_size = PyBytes_GET_SIZE(ad_source_obj);
  if (ad_base == NULL || ad_size < 0) {
    PyErr_SetString(PyExc_ValueError, "invalid ad_source bytes");
    return NULL;
  }

  // Allocate state.
  FObj* fobjs = NULL;
  float* cur_rot = NULL;
  float* cur_pos = NULL;
  float* cur_scl = NULL;
  float* world_mtx = NULL;
  float* world_scl = NULL;
  bool* world_scl_valid = NULL;
  PyObject* out_mats = NULL;
  PyObject* out_locals = NULL;
  PyObject* out_transn = NULL;
  PyObject* ret = NULL;

  if (num_fobjs > 0) {
    fobjs = (FObj*)PyMem_Malloc((size_t)num_fobjs * sizeof(FObj));
    if (fobjs == NULL) {
      PyErr_NoMemory();
      goto cleanup;
    }
  }

  cur_rot = (float*)PyMem_Malloc((size_t)parts_num * 3 * sizeof(float));
  cur_pos = (float*)PyMem_Malloc((size_t)parts_num * 3 * sizeof(float));
  cur_scl = (float*)PyMem_Malloc((size_t)parts_num * 3 * sizeof(float));
  world_mtx = (float*)PyMem_Malloc((size_t)parts_num * 12 * sizeof(float));
  world_scl = (float*)PyMem_Malloc((size_t)parts_num * 3 * sizeof(float));
  world_scl_valid = (bool*)PyMem_Malloc((size_t)parts_num * sizeof(bool));
  if (cur_rot == NULL || cur_pos == NULL || cur_scl == NULL || world_mtx == NULL ||
      world_scl == NULL || world_scl_valid == NULL) {
    PyErr_NoMemory();
    goto cleanup;
  }

  // Output buffers (little-endian float32 bytes).
  const npy_intp local_count = PyArray_DIM(local_parts, 0);
  const npy_intp joint_count = PyArray_DIM(joint_parts, 0);
  const size_t mats_f32 = (size_t)frame_count * (size_t)joint_count * 12u;
  const size_t locals_f32 = (size_t)frame_count * (size_t)local_count * 9u;
  const size_t transn_f32 = (size_t)frame_count * 3u;
  out_mats = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)(mats_f32 * 4u));
  out_locals = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)(locals_f32 * 4u));
  out_transn = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)(transn_f32 * 4u));
  if (out_mats == NULL || out_locals == NULL || out_transn == NULL) {
    goto cleanup;
  }
  float* mats_ptr = (float*)PyBytes_AS_STRING(out_mats);
  float* locals_ptr = (float*)PyBytes_AS_STRING(out_locals);
  float* transn_ptr = (float*)PyBytes_AS_STRING(out_transn);

  // Initialize current SRT from rest pose (and apply inv scale override).
  const float* rest_rot_ptr = (const float*)PyArray_DATA(rest_rot);
  const float* rest_pos_ptr = (const float*)PyArray_DATA(rest_pos);
  const float* rest_scl_ptr = (const float*)PyArray_DATA(rest_scl);
  for (npy_intp i = 0; i < parts_num; i++) {
    cur_rot[i * 3 + 0] = rest_rot_ptr[i * 3 + 0];
    cur_rot[i * 3 + 1] = rest_rot_ptr[i * 3 + 1];
    cur_rot[i * 3 + 2] = rest_rot_ptr[i * 3 + 2];
    cur_pos[i * 3 + 0] = rest_pos_ptr[i * 3 + 0];
    cur_pos[i * 3 + 1] = rest_pos_ptr[i * 3 + 1];
    cur_pos[i * 3 + 2] = rest_pos_ptr[i * 3 + 2];
    cur_scl[i * 3 + 0] = rest_scl_ptr[i * 3 + 0];
    cur_scl[i * 3 + 1] = rest_scl_ptr[i * 3 + 1];
    cur_scl[i * 3 + 2] = rest_scl_ptr[i * 3 + 2];
  }
  if (inv_scale_part >= 0 && (npy_intp)inv_scale_part < parts_num) {
    cur_scl[inv_scale_part * 3 + 0] = inv_model_scale;
    cur_scl[inv_scale_part * 3 + 1] = inv_model_scale;
    cur_scl[inv_scale_part * 3 + 2] = inv_model_scale;
  }

  // Initialize FObjs.
  const uint32_t* fobj_desc_ptr = (const uint32_t*)PyArray_DATA(fobj_desc);
  for (npy_intp i = 0; i < num_fobjs; i++) {
    const uint32_t d0 = fobj_desc_ptr[i * 3 + 0];
    const uint32_t d1 = fobj_desc_ptr[i * 3 + 1];
    const uint32_t d2 = fobj_desc_ptr[i * 3 + 2];
    const uint8_t obj_type = (uint8_t)(d0 & 0xFFu);
    const uint8_t frac_value = (uint8_t)((d0 >> 8) & 0xFFu);
    const uint8_t frac_slope = (uint8_t)((d0 >> 16) & 0xFFu);
    const int startframe = (int)(d1 & 0xFFFFu);
    const int length = (int)((d1 >> 16) & 0xFFFFu);
    const uint32_t ad_off = d2;
    if ((Py_ssize_t)ad_off + (Py_ssize_t)length > ad_size) {
      PyErr_SetString(PyExc_ValueError, "fobj ad range out of bounds of ad_source");
      goto cleanup;
    }
    FObj* fo = &fobjs[i];
    memset(fo, 0, sizeof(FObj));
    fo->ad = (const uint8_t*)(ad_base + (Py_ssize_t)ad_off);
    fo->length = length;
    fo->startframe = startframe;
    fo->obj_type = obj_type;
    fo->frac_value = frac_value;
    fo->frac_slope = frac_slope;
    fobj_req_anim(fo, 0.0f);
  }

  static const float IDENTITY[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                     0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

  const int16_t* parent_ptr = (const int16_t*)PyArray_DATA(parent_part);
  const uint32_t* flags_ptr = (const uint32_t*)PyArray_DATA(part_flags);
  const int32_t* order_ptr = (const int32_t*)PyArray_DATA(order);
  const int32_t* local_parts_ptr = (const int32_t*)PyArray_DATA(local_parts);
  const int32_t* joint_parts_ptr = (const int32_t*)PyArray_DATA(joint_parts);
  const int32_t* update_parts_ptr = (const int32_t*)PyArray_DATA(update_parts);
  const npy_intp order_n = PyArray_DIM(order, 0);

  for (npy_intp i = 0; i < parts_num; i++) {
    memcpy(&world_mtx[i * 12], IDENTITY, sizeof(IDENTITY));
    world_scl_valid[i] = false;
  }

  size_t mats_out_i = 0;
  size_t locals_out_i = 0;

  for (int frame = 0; frame < frame_count; frame++) {
    const float rate = (frame == 0) ? 0.0f : 1.0f;
    const bool should_stop_aobj = (aobj_loop == 0 && end_frame > 0.0f && (float)frame >= end_frame);

    for (npy_intp up_i = 0; up_i < update_parts_n; up_i++) {
      const int part = update_parts_ptr[up_i];
      const int32_t start = fobj_starts_ptr[up_i];
      const int32_t end = fobj_starts_ptr[up_i + 1];
      for (int32_t fi = start; fi < end; fi++) {
        float v = 0.0f;
        if (!fobj_interpret(&fobjs[fi], rate, &v)) {
          continue;
        }
        const uint8_t obj_type = fobjs[fi].obj_type;
        float* r = &cur_rot[part * 3];
        float* p = &cur_pos[part * 3];
        float* s = &cur_scl[part * 3];
        if (obj_type == 1) {
          r[0] = v;
        } else if (obj_type == 2) {
          r[1] = v;
        } else if (obj_type == 3) {
          r[2] = v;
        } else if (obj_type == 5) {
          p[0] = v;
        } else if (obj_type == 6) {
          p[1] = v;
        } else if (obj_type == 7) {
          p[2] = v;
        } else if (obj_type == 8) {
          const double av = fabs((double)v);
          // Python: _f32(max(abs(v), 1.0e-3)) with abs(v) as the first argument.
          // For NaNs, Python's max keeps the first argument; match that.
          s[0] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
        } else if (obj_type == 9) {
          const double av = fabs((double)v);
          s[1] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
        } else if (obj_type == 10) {
          const double av = fabs((double)v);
          s[2] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
        }
      }
      if (should_stop_aobj) {
        for (int32_t fi = start; fi < end; fi++) {
          float v = 0.0f;
          const bool have_terminal = fobj_stopped_terminal_value(&fobjs[fi], &v);
          if (!have_terminal && !fobj_stop_anim(&fobjs[fi], 1.0f, &v)) {
            continue;
          }
          if (have_terminal) {
            fobjs[fi].state = 0;
          }
          const uint8_t obj_type = fobjs[fi].obj_type;
          float* r = &cur_rot[part * 3];
          float* p = &cur_pos[part * 3];
          float* s = &cur_scl[part * 3];
          if (obj_type == 1) {
            r[0] = v;
          } else if (obj_type == 2) {
            r[1] = v;
          } else if (obj_type == 3) {
            r[2] = v;
          } else if (obj_type == 5) {
            p[0] = v;
          } else if (obj_type == 6) {
            p[1] = v;
          } else if (obj_type == 7) {
            p[2] = v;
          } else if (obj_type == 8) {
            const double av = fabs((double)v);
            s[0] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
          } else if (obj_type == 9) {
            const double av = fabs((double)v);
            s[1] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
          } else if (obj_type == 10) {
            const double av = fabs((double)v);
            s[2] = f32_from_double((av < 1.0e-3) ? 1.0e-3 : av);
          }
        }
      }
    }

    if (parts_num > 1) {
      transn_ptr[frame * 3 + 0] = cur_pos[1 * 3 + 0];
      transn_ptr[frame * 3 + 1] = cur_pos[1 * 3 + 1];
      transn_ptr[frame * 3 + 2] = cur_pos[1 * 3 + 2];
      cur_pos[1 * 3 + 0] = 0.0f;
      cur_pos[1 * 3 + 1] = 0.0f;
      cur_pos[1 * 3 + 2] = 0.0f;
    } else {
      transn_ptr[frame * 3 + 0] = 0.0f;
      transn_ptr[frame * 3 + 1] = 0.0f;
      transn_ptr[frame * 3 + 2] = 0.0f;
    }

    for (npy_intp oi = 0; oi < order_n; oi++) {
      const int part = order_ptr[oi];
      const int pidx = (int)parent_ptr[part];
      const float* parent_m = (pidx >= 0) ? &world_mtx[pidx * 12] : IDENTITY;
      const float* parent_s = (pidx >= 0 && world_scl_valid[pidx]) ? &world_scl[pidx * 3] : NULL;

      float local_m[12];
      float world_m[12];
      mtx_srt(local_m, &cur_scl[part * 3], &cur_rot[part * 3], &cur_pos[part * 3], parent_s);
      mtx_concat(world_m, parent_m, local_m);
      memcpy(&world_mtx[part * 12], world_m, sizeof(world_m));

      if ((flags_ptr[part] & 8u) != 0u) {
        if (pidx >= 0 && world_scl_valid[pidx]) {
          world_scl[part * 3 + 0] = world_scl[pidx * 3 + 0];
          world_scl[part * 3 + 1] = world_scl[pidx * 3 + 1];
          world_scl[part * 3 + 2] = world_scl[pidx * 3 + 2];
          world_scl_valid[part] = true;
        } else {
          world_scl_valid[part] = false;
        }
      } else {
        if (pidx >= 0 && world_scl_valid[pidx]) {
          const float psx = world_scl[pidx * 3 + 0];
          const float psy = world_scl[pidx * 3 + 1];
          const float psz = world_scl[pidx * 3 + 2];
          world_scl[part * 3 + 0] = f32_mul_d((double)cur_scl[part * 3 + 0], (double)psx);
          world_scl[part * 3 + 1] = f32_mul_d((double)cur_scl[part * 3 + 1], (double)psy);
          world_scl[part * 3 + 2] = f32_mul_d((double)cur_scl[part * 3 + 2], (double)psz);
          world_scl_valid[part] = true;
        } else {
          world_scl[part * 3 + 0] = cur_scl[part * 3 + 0];
          world_scl[part * 3 + 1] = cur_scl[part * 3 + 1];
          world_scl[part * 3 + 2] = cur_scl[part * 3 + 2];
          world_scl_valid[part] = true;
        }
      }
    }

    for (npy_intp li = 0; li < local_count; li++) {
      const int part = local_parts_ptr[li];
      locals_ptr[locals_out_i++] = cur_rot[part * 3 + 0];
      locals_ptr[locals_out_i++] = cur_rot[part * 3 + 1];
      locals_ptr[locals_out_i++] = cur_rot[part * 3 + 2];
      locals_ptr[locals_out_i++] = cur_pos[part * 3 + 0];
      locals_ptr[locals_out_i++] = cur_pos[part * 3 + 1];
      locals_ptr[locals_out_i++] = cur_pos[part * 3 + 2];
      locals_ptr[locals_out_i++] = cur_scl[part * 3 + 0];
      locals_ptr[locals_out_i++] = cur_scl[part * 3 + 1];
      locals_ptr[locals_out_i++] = cur_scl[part * 3 + 2];
    }

    for (npy_intp ji = 0; ji < joint_count; ji++) {
      const int part = joint_parts_ptr[ji];
      memcpy(&mats_ptr[mats_out_i], &world_mtx[part * 12], 12 * sizeof(float));
      mats_out_i += 12;
    }
  }

  ret = Py_BuildValue("(OOO)", out_mats, out_locals, out_transn);

cleanup:
  Py_XDECREF(out_mats);
  Py_XDECREF(out_locals);
  Py_XDECREF(out_transn);
  if (fobjs) {
    PyMem_Free(fobjs);
  }
  if (cur_rot) {
    PyMem_Free(cur_rot);
  }
  if (cur_pos) {
    PyMem_Free(cur_pos);
  }
  if (cur_scl) {
    PyMem_Free(cur_scl);
  }
  if (world_mtx) {
    PyMem_Free(world_mtx);
  }
  if (world_scl) {
    PyMem_Free(world_scl);
  }
  if (world_scl_valid) {
    PyMem_Free(world_scl_valid);
  }
  return ret;
}

static PyObject* msl_destroy(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* capsule = NULL;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h->prev_input_storage);
  h->prev_input_storage = NULL;
  PyMem_Free(h->input_storage);
  h->input_storage = NULL;
  Py_RETURN_NONE;
}

static PyObject* msl_move_tables_debug_query_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_has_release_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_release_frame_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_release_hit_idx_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_hitbox_params_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_cmd1_active_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_should_spawn_projectile_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_should_flip_facing_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_throw_crossed_projectile_pulse_frame_py(PyObject* self,
                                                                         PyObject* args) {
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

static PyObject* msl_move_tables_throw_projectile_first_pulse_frame_py(PyObject* self,
                                                                       PyObject* args) {
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

static PyObject* msl_move_tables_throw_projectile_last_pulse_frame_py(PyObject* self,
                                                                      PyObject* args) {
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

static PyObject* msl_move_tables_throw_projectile_pulse_ordinal_py(PyObject* self, PyObject* args) {
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

static PyObject* msl_move_tables_special_pseudo_random_sfx_ranges_crossed_py(PyObject* self,
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

static PyObject* msl_debug_hitlist_fighter_contains_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* capsule = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  int victim = 0;
  if (!PyArg_ParseTuple(args, "Oiiii", &capsule, &batch_index, &attacker, &hb_id, &victim)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid handle");
    return NULL;
  }

  int present = 0;
  const int err = msl_batch_debug_hitlist_fighter_contains(h->batch, batch_index, attacker, hb_id,
                                                           victim, &present);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitlist_fighter_contains failed: %d", err);
    return NULL;
  }
  return PyLong_FromLong((long)present);
}

static PyObject* msl_debug_hitlist_fighter_capsule_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* capsule = NULL;
  int batch_index = 0;
  int attacker = 0;
  int hb_id = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &capsule, &batch_index, &attacker, &hb_id)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid handle");
    return NULL;
  }

  npy_intp dims[2] = {(npy_intp)1, (npy_intp)sizeof(MslDebugHitlistCapsule)};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT8);
  if (arr == NULL) {
    return NULL;
  }

  MslDebugHitlistCapsule* out = (MslDebugHitlistCapsule*)PyArray_DATA(arr);
  const int err =
      msl_batch_debug_hitlist_fighter_capsule(h->batch, batch_index, attacker, hb_id, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitlist_fighter_capsule failed: %d", err);
    return NULL;
  }

  return (PyObject*)arr;
}

static PyObject* msl_hitlist_ring_demo_py(PyObject* self, PyObject* args) {
  (void)self;
  int inserts = 0;
  if (!PyArg_ParseTuple(args, "i", &inserts)) {
    return NULL;
  }
  if (inserts < 0) {
    inserts = 0;
  }
  if (inserts > 64) {
    inserts = 64;
  }

  MslHitlistCapsule hit;
  hitlist_capsule_clear(&hit);
  for (int i = 0; i < inserts; i++) {
    hitlist_debug_insert_item_victims1(&hit, 0, (uint32_t)(i + 1u), 0);
  }

  npy_intp dims[1] = {(npy_intp)MSL_HITLIST_VICTIM_CAP};
  PyObject* out_ids = PyArray_SimpleNew(1, dims, NPY_UINT32);
  if (out_ids == NULL) {
    return NULL;
  }
  uint32_t* ids_ptr = (uint32_t*)PyArray_DATA((PyArrayObject*)out_ids);
  for (int i = 0; i < MSL_HITLIST_VICTIM_CAP; i++) {
    const MslHitlistVictimEntry* e = &hit.victims_1[i];
    ids_ptr[i] = (e->kind_slot == 0xFFu) ? 0u : e->id32;
  }

  PyObject* ret = Py_BuildValue("(iO)", (int)hit.ring_1, out_ids);
  Py_DECREF(out_ids);
  return ret;
}

static PyObject* msl_hitlist_insert_cd_demo_py(PyObject* self, PyObject* args) {
  (void)self;
  int type = 0;
  int rehit_frames = 0;
  if (!PyArg_ParseTuple(args, "ii", &type, &rehit_frames)) {
    return NULL;
  }
  if (rehit_frames < 0) {
    rehit_frames = 0;
  }
  if (rehit_frames > 255) {
    rehit_frames = 255;
  }

  MslHitlistCapsule hit;
  hitlist_capsule_clear(&hit);
  hitlist_debug_insert_item_victims1(&hit, type, 1u, (uint8_t)rehit_frames);
  return PyLong_FromLong((long)hit.victims_1[0].cd);
}

static PyMethodDef methods[] = {
    {"init", (PyCFunction)(void (*)(void))msl_init, METH_VARARGS | METH_KEYWORDS,
     "init(batch_size, num_players, ucf_enabled=?, ucf_cardinals_1_0_enabled=?) -> handle"},
    {"destroy", msl_destroy, METH_VARARGS,
     "destroy(handle) -> None (free underlying C batch immediately)"},
    {"reseed_seed", msl_reseed_seed, METH_VARARGS,
     "reseed_seed(handle, seed_bytes[batch, seed_stride])"},
    {"reseed_seed_rollout", msl_reseed_seed_rollout, METH_VARARGS,
     "reseed_seed_rollout(handle, seed_bytes[batch, seed_stride])"},
    {"init_match", msl_init_match, METH_VARARGS,
     "init_match(handle, match_config_bytes[batch, match_config_stride])"},
    {"init_match_masked", msl_init_match_masked, METH_VARARGS,
     "init_match_masked(handle, match_config_bytes[batch, match_config_stride], mask[batch])"},
    {"debug_copy_lanes", msl_debug_copy_lanes_py, METH_VARARGS,
     "debug_copy_lanes(dst_handle, src_handle, dst_lanes[int32], src_lanes[int32]) -> "
     "DEBUG-ONLY. Exercise msl_batch_copy_lanes for focused API tests."},
    {"debug_copy_colldata", msl_debug_copy_colldata_py, METH_VARARGS,
     "debug_copy_colldata(handle, batch_index, src_player, dst_player) -> DEBUG-ONLY. Copy modeled "
     "CollData lanes without routing action owners."},
    {"step_input", msl_step_input, METH_VARARGS,
     "step_input(handle, prev_input_bytes, input_bytes)"},
    {"debug_step_input_pre_combat", msl_debug_step_input_pre_combat, METH_VARARGS,
     "debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes) -> DEBUG-ONLY triage "
     "step. Advances/mutates state through pre-combat stages, deliberately skips combat_resolve(), "
     "and is not comparable to step_input() for training/rollouts."},
    {"debug_knockdown_update_pre_physics", msl_debug_knockdown_update_pre_physics, METH_VARARGS,
     "debug_knockdown_update_pre_physics(handle) -> DEBUG-ONLY branch isolation. Runs only the "
     "knockdown/damage pre-physics callback slice on the current reseeded state; not for "
     "training/rollouts."},
    {"debug_set_coll_env_flags", msl_debug_set_coll_env_flags_py, METH_VARARGS,
     "debug_set_coll_env_flags(handle, batch_index, player_index, flags) -> DEBUG-ONLY. Override "
     "coll_env_flags for a single fighter."},
    {"debug_set_mpcoll_joint_filters", msl_debug_set_mpcoll_joint_filters_py, METH_VARARGS,
     "debug_set_mpcoll_joint_filters(handle, batch_index, player_index, joint_skip, joint_only) -> "
     "DEBUG-ONLY. Override CollData joint filters (-1 disables each)."},
    {"debug_set_escapeair_floor_producer_runtime",
     msl_debug_set_escapeair_floor_producer_runtime_py, METH_VARARGS,
     "debug_set_escapeair_floor_producer_runtime(handle, batch_index, player_index, authority, "
     "desired_owner) -> DEBUG-ONLY. Override EscapeAir floor-producer authority for tests."},
    {"debug_set_floor_sweep_prev_runtime", msl_debug_set_floor_sweep_prev_runtime_py, METH_VARARGS,
     "debug_set_floor_sweep_prev_runtime(handle, batch_index, player_index, x, y, authority) -> "
     "DEBUG-ONLY. Override runtime floor-sweep provenance for tests."},
    {"debug_set_wall_ceil_prev_runtime", msl_debug_set_wall_ceil_prev_runtime_py, METH_VARARGS,
     "debug_set_wall_ceil_prev_runtime(handle, batch_index, player_index, x, y, authority) -> "
     "DEBUG-ONLY. Override runtime wall/ceiling previous-root provenance for tests."},
    {"debug_set_player_root", msl_debug_set_player_root_py, METH_VARARGS,
     "debug_set_player_root(handle, batch_index, player_index, pos_x, pos_y, facing) -> "
     "DEBUG-ONLY. Override fighter root position/facing for fixture setup."},
    {"debug_set_ceiling_contact", msl_debug_set_ceiling_contact_py, METH_VARARGS,
     "debug_set_ceiling_contact(handle, batch_index, player_index, contact_y) -> DEBUG-ONLY. "
     "Override ceiling_contact_y for a single fighter."},
    {"debug_run_knockdown_post_collision", msl_debug_run_knockdown_post_collision_py, METH_VARARGS,
     "debug_run_knockdown_post_collision(handle) -> DEBUG-ONLY. Re-run "
     "knockdown_update_post_collision on current batch state."},
    {"debug_run_locomotion_post_collision", msl_debug_run_locomotion_post_collision_py,
     METH_VARARGS,
     "debug_run_locomotion_post_collision(handle) -> DEBUG-ONLY. Re-run "
     "locomotion_update_post_collision on current batch state."},
    {"debug_refresh_combat_geometry", msl_debug_refresh_combat_geometry, METH_VARARGS,
     "debug_refresh_combat_geometry(handle) -> DEBUG-ONLY. Recompute hurtcaps/hitboxes from "
     "current state without advancing frame stages."},
    {"write_compare", msl_write_compare, METH_VARARGS, "write_compare(handle, out_bytes)"},
    {"bind_buffers", msl_bind_buffers, METH_VARARGS,
     "bind_buffers(handle, match_config, prev_input, input, compare[, viewpoint, "
     "gamestate, terminal])"},
    {"unbind_buffers", msl_unbind_buffers, METH_VARARGS, "unbind_buffers(handle)"},
    {"bind_sequence_buffers", msl_bind_sequence_buffers, METH_VARARGS,
     "bind_sequence_buffers(handle, match_config, action, compare, viewpoint, gamestate, "
     "terminal, done, reset_mask, action_format)"},
    {"unbind_sequence_buffers", msl_unbind_sequence_buffers, METH_VARARGS,
     "unbind_sequence_buffers(handle)"},
    {"init_match_bound", msl_init_match_bound, METH_VARARGS,
     "init_match_bound(handle) -> initialize from bound match_config"},
    {"init_match_sequence_bound", msl_init_match_sequence_bound, METH_VARARGS,
     "init_match_sequence_bound(handle) -> initialize from bound sequence match_config"},
    {"reset_sequence_masked", msl_reset_sequence_masked, METH_VARARGS,
     "reset_sequence_masked(handle, frame, write_initial_observation=True)"},
    {"reset_prev_input", msl_reset_prev_input, METH_VARARGS,
     "reset_prev_input(handle) -> clear env-owned previous input storage"},
    {"set_prev_input_from_sequence", msl_set_prev_input_from_sequence, METH_VARARGS,
     "set_prev_input_from_sequence(handle, frame) -> copy sequence action[frame] to previous "
     "input"},
    {"step_bound", msl_step_bound, METH_VARARGS,
     "step_bound(handle) -> step using bound prev_input/input buffers"},
    {"step_sequence", msl_step_sequence, METH_VARARGS,
     "step_sequence(handle, frame, write_outputs=True, write_compare=False, max_frame_id=-1)"},
    {"write_compare_bound", msl_write_compare_bound, METH_VARARGS,
     "write_compare_bound(handle) -> write bound compare buffer"},
    {"step_write_compare_bound", msl_step_write_compare_bound, METH_VARARGS,
     "step_write_compare_bound(handle) -> fused step + compare write using bound buffers"},
    {"write_gamestate_bound", msl_write_gamestate_bound, METH_VARARGS,
     "write_gamestate_bound(handle) -> write bound gamestate buffer"},
    {"write_terminal_bound", msl_write_terminal_bound, METH_VARARGS,
     "write_terminal_bound(handle, max_frame_id=-1) -> write bound terminal buffer"},
    {"collect_mismatch_events", msl_collect_mismatch_events, METH_VARARGS,
     "collect_mismatch_events(seed_bytes, ref_bytes, out_bytes, num_players) -> dict[np.ndarray]. "
     "Native scanner for tools.eval.mismatch_taxonomy strict compare rows."},
    {"disruptive_scan", msl_disruptive_scan, METH_VARARGS,
     "disruptive_scan(samples_u8, horizons, discrete_fields, float_fields, players, num_players, "
     "max_records, stride, float_epsilon, ucf_enabled, ucf_cardinals_enabled, batch_size, "
     "start_record, stop_record, profile_rl1) -> (int64[:,22], float64[:,6])"},
    {"write_gamestate", msl_write_gamestate, METH_VARARGS,
     "write_gamestate(handle, viewpoint_players[batch], out_bytes)"},
    {"write_terminal", msl_write_terminal, METH_VARARGS,
     "write_terminal(handle, out_bytes, max_frame_id=-1)"},
    {"debug_write_processed_input", msl_debug_write_processed_input, METH_VARARGS,
     "debug_write_processed_input(handle, out_bytes)"},
    {"debug_write_stage_state", msl_debug_write_stage_state, METH_VARARGS,
     "debug_write_stage_state(handle, out_bytes)"},
    {"debug_stage_moving_floor_surface", msl_debug_stage_moving_floor_surface_py, METH_VARARGS,
     "debug_stage_moving_floor_surface(handle, batch_index, segment_i) -> transformed floor "
     "surface packet or None"},
    {"debug_write_internals", msl_debug_write_internals, METH_VARARGS,
     "debug_write_internals(handle, out_bytes)"},
    {"debug_write_collision_contacts", msl_debug_write_collision_contacts, METH_VARARGS,
     "debug_write_collision_contacts(handle, out_bytes)"},
    {"debug_write_colldata_ecb", msl_debug_write_colldata_ecb, METH_VARARGS,
     "debug_write_colldata_ecb(handle, out_bytes)"},
    {"debug_force_anim_timebase_enter", msl_debug_force_anim_timebase_enter, METH_VARARGS,
     "debug_force_anim_timebase_enter(handle, batch_index, player_index, anim_start, anim_speed)"},
    {"debug_set_rollout_clock_mode", msl_debug_set_rollout_clock_mode_py, METH_VARARGS,
     "debug_set_rollout_clock_mode(handle, batch_index, mode)"},
    {"debug_get_rollout_clock_mode", msl_debug_get_rollout_clock_mode_py, METH_VARARGS,
     "debug_get_rollout_clock_mode(handle, batch_index) -> int"},
    {"debug_set_camera_mode", msl_debug_set_camera_mode_py, METH_VARARGS,
     "debug_set_camera_mode(handle, batch_index, mode)"},
    {"debug_timebase", msl_debug_timebase_py, METH_VARARGS,
     "debug_timebase(handle, batch_index) -> np.ndarray[float32] shape=(MSL_MAX_PLAYERS,8)"},
    {"debug_hitbox_event_timing", msl_debug_hitbox_event_timing_py, METH_VARARGS,
     "debug_hitbox_event_timing(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitboxEventTiming)]"},
    {"debug_hitbox_sweep_proxy", msl_debug_hitbox_sweep_proxy_py, METH_VARARGS,
     "debug_hitbox_sweep_proxy(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitboxSweepProxy)]"},
    {"debug_hurtcap_slot_flags", msl_debug_hurtcap_slot_flags_py, METH_VARARGS,
     "debug_hurtcap_slot_flags(handle, batch_index, player_index, cap_id) -> "
     "bytes[1,sizeof(MslDebugHurtcapSlotFlags)]"},
    {"debug_hurtcap_geometry_valid", msl_debug_hurtcap_geometry_valid_py, METH_VARARGS,
     "debug_hurtcap_geometry_valid(handle, batch_index, player_index) -> 0/1"},
    {"debug_hurtcap_matrix_valid", msl_debug_hurtcap_matrix_valid_py, METH_VARARGS,
     "debug_hurtcap_matrix_valid(handle, batch_index, player_index, cap_id) -> 0/1"},
    {"debug_poison_hurtcap_matrix", msl_debug_poison_hurtcap_matrix_py, METH_VARARGS,
     "debug_poison_hurtcap_matrix(handle, batch_index, player_index, cap_id)"},
    {"debug_dynamic_pose_state", msl_debug_dynamic_pose_state_py, METH_VARARGS,
     "debug_dynamic_pose_state(handle, batch_index, player_index) -> "
     "bytes[1,sizeof(MslDebugDynamicPoseState)]"},
    {"debug_get_fighter_8006cda4_pre_gate_consume_count",
     msl_debug_get_fighter_8006cda4_pre_gate_consume_count_py, METH_VARARGS,
     "debug_get_fighter_8006cda4_pre_gate_consume_count(handle, batch_index, player_index) -> int"},
    {"debug_attackairb_continuation_overlap", msl_debug_attackairb_continuation_overlap_py,
     METH_VARARGS,
     "debug_attackairb_continuation_overlap(handle, batch_index, attacker, hb_id, defender, "
     "cap_id) -> float"},
    {"debug_body_matrix_overlap", msl_debug_body_matrix_overlap_py, METH_VARARGS,
     "debug_body_matrix_overlap(handle, batch_index, attacker, hb_id, defender, cap_id) -> float"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
    {"data_schema_versions", msl_data_schema_versions, METH_NOARGS,
     "data_schema_versions() -> dict of extracted data schema versions expected by this runtime"},
    {"alloc_reset", msl_alloc_reset, METH_NOARGS,
     "Reset C allocation counters (debug/perf guardrail)."},
    {"alloc_stats", msl_alloc_stats, METH_NOARGS,
     "Get C allocation counters (debug/perf guardrail)."},
    {"char_params_ecb_joints", msl_char_params_ecb_joints_py, METH_VARARGS,
     "char_params_ecb_joints(char_id) -> list[int] loaded from data/characters/<char>.json."},
    {"char_params_part_anchors", msl_char_params_part_anchors_py, METH_VARARGS,
     "char_params_part_anchors(char_id) -> dict of runtime part anchors from character data."},
    {"item_article_params", msl_item_article_params_py, METH_VARARGS,
     "item_article_params(char_id) -> dict loaded from MSLITAR1."},
    {"stage_floor_segment", msl_stage_floor_segment_py, METH_VARARGS,
     "stage_floor_segment(stage_id, segment_i) -> dict from runtime stage collision tables."},
    {"stage_topology_flags", msl_stage_topology_flags_py, METH_VARARGS,
     "stage_topology_flags(stage_id) -> generated stage topology predicate dict."},
    {"stage_fighter_floor_segment", msl_stage_fighter_floor_segment_py, METH_VARARGS,
     "stage_fighter_floor_segment(stage_id, segment_i) -> dict from fighter-solid floor tables."},
    {"stage_ceiling_segment", msl_stage_ceiling_segment_py, METH_VARARGS,
     "stage_ceiling_segment(stage_id, segment_i) -> dict from runtime ceiling tables."},
    {"stage_left_wall_segment", msl_stage_left_wall_segment_py, METH_VARARGS,
     "stage_left_wall_segment(stage_id, segment_i) -> dict from runtime left-wall tables."},
    {"stage_right_wall_segment", msl_stage_right_wall_segment_py, METH_VARARGS,
     "stage_right_wall_segment(stage_id, segment_i) -> dict from runtime right-wall tables."},
    {"stage_raw_line_non_kind", msl_stage_raw_line_non_kind_py, METH_VARARGS,
     "stage_raw_line_non_kind(stage_id, segment_i, skip_kind, forward) -> raw MapLine neighbor."},
    {"stage_static_query", msl_stage_static_query_py, METH_VARARGS,
     "stage_static_query(stage_id, checks, x0, y0, x1, y1, line_skip, joint_skip, joint_only) -> "
     "static mpLib-style line hit dict or None."},
    {"mpcoll_check_bounding_aabb", msl_mpcoll_check_bounding_aabb_py, METH_VARARGS,
     "mpcoll_check_bounding_aabb(prev_x, prev_y, cur_x, cur_y, prev_l, prev_r, prev_b, prev_t, "
     "cur_l, cur_r, cur_b, cur_t, flags, ledge_snap_x, ledge_snap_y, ledge_snap_h) -> dict"},
    {"mpcoll_end_publication", msl_mpcoll_end_publication_py, METH_VARARGS,
     "mpcoll_end_publication(floor_id, ceiling_id, env_flags, force_floor, floor_arg2, cur_y, "
     "last_y) -> static mpCollEnd finalizer event dict"},
    {"stage_match_flow_roles", msl_stage_match_flow_roles_py, METH_VARARGS,
     "stage_match_flow_roles(stage_id) -> dict of runtime MSLSTG01 match-flow role data."},
    {"hitlist_ring_demo", msl_hitlist_ring_demo_py, METH_VARARGS,
     "hitlist_ring_demo(inserts) -> (ring, ids_u32[12]) (test-only)"},
    {"hitlist_insert_cd_demo", msl_hitlist_insert_cd_demo_py, METH_VARARGS,
     "hitlist_insert_cd_demo(type, rehit_frames) -> inserted victims_1 cooldown (test-only)"},
    {"debug_reset_pose_and_hitboxes_tables", msl_debug_reset_pose_and_hitboxes_tables_py,
     METH_NOARGS, "Reset pose+hitbox global tables (test-only)."},
    {"pose_points_world", msl_pose_points_world_py, METH_VARARGS,
     "pose_points_world(matrices[:,12], local_xyz[:,3], count, model_scale, facing_dir, px, py, "
     "pz, out_xyz[:,3]) -> None"},
    {"derive_camera_target_world", msl_derive_camera_target_world_py, METH_VARARGS,
     "derive_camera_target_world(char_id, animation_index, anim_frame, scale_y, facing, pos_x, "
     "pos_y, pos_z) -> (x,y,z,radius)"},
    {"derive_hitbox_prev_centers", msl_derive_hitbox_prev_centers_py, METH_VARARGS,
     "derive_hitbox_prev_centers(num_players, char_id, action_id, animation_index, action_frame, "
     "anim_frame, pos_x, pos_y, pos_z_or_None, facing, scale_y, rotate_model_or_None, "
     "rotate_valid_or_None) -> (valid,x,y,z)"},
    {"derive_combo_push_timer_seed", msl_derive_combo_push_timer_seed_py, METH_VARARGS,
     "derive_combo_push_timer_seed(combo_count, last_attack_landed, combo_victim_port=None) -> "
     "uint16[:,4]"},
    {"derive_combo_seed_fields", msl_derive_combo_seed_fields_py, METH_VARARGS,
     "derive_combo_seed_fields(num_players, src_ports, hitlag, state_flags, instance_id, "
     "last_hit_by, instance_hit_by=None) -> (victim_port, victim_iid, timer)"},
    {"derive_instance_id_x2073", msl_derive_instance_id_x2073_py, METH_VARARGS,
     "derive_instance_id_x2073(char_id, action_id, action_frame) -> uint8[:]"},
    {"derive_instance_id_counter", msl_derive_instance_id_counter_py, METH_VARARGS,
     "derive_instance_id_counter(fighter_instance_id, item_instance_id) -> uint16[:]"},
    {"derive_item_spawn_id_counter", msl_derive_item_spawn_id_counter_py, METH_VARARGS,
     "derive_item_spawn_id_counter(item_exists, item_spawn_id) -> uint32[:]"},
    {"derive_staling_history", msl_derive_staling_history_py, METH_VARARGS,
     "derive_staling_history(src_ports, char_id, action_id, action_frame, animation_index, "
     "percent, stocks, instance_id, last_hit_by, last_hit_by_instance[, item_exists, item_owner, "
     "item_instance_id, item_attack_id, item_attack_instance]) -> "
     "(attack_id, attack_instance, stale_queue_index, stale_move_id, stale_attack_instance)"},
    {"process_stick_i8_units", msl_process_stick_i8_units_py, METH_VARARGS,
     "process_stick_i8_units(raw_x, raw_y, ucf_enabled, cardinals_enabled, deadzone_x, "
     "deadzone_y) -> (proc_x_i8, proc_y_i8, unit_x_f32, unit_y_f32)"},
    {"derive_ucf_pad_buffer_state", msl_derive_ucf_pad_buffer_state_py, METH_VARARGS,
     "derive_ucf_pad_buffer_state(raw_x, raw_y, stick_y_hold_time, ucf_enabled, "
     "cardinals_enabled, deadzone_x, deadzone_y) -> (index, sdrop_up, stick_x[:,4], stick_y[:,4])"},
    {"compute_tilt_timer_axis", msl_compute_tilt_timer_axis_py, METH_VARARGS,
     "compute_tilt_timer_axis(axis_unit, tilt_thresh, start_timer) -> uint8[:]"},
    {"compute_tilt_timer_axis_pre_post", msl_compute_tilt_timer_axis_pre_post_py, METH_VARARGS,
     "compute_tilt_timer_axis_pre_post(axis_unit, tilt_thresh, override_or_None, "
     "override_value, reset_or_None, start_timer_post) -> (pre, post)"},
    {"compute_tilt_timer_y_pre_post_with_fall_fast",
     msl_compute_tilt_timer_y_pre_post_with_fall_fast_py, METH_VARARGS,
     "compute_tilt_timer_y_pre_post_with_fall_fast(stick_y, tilt_thresh, jump_entry, "
     "pre_input_jump_entry_or_None, fastfall_ok, speed_y, on_ground, fastfall_stick_threshold, "
     "fastfall_tilt_max_frames, reset_or_None, start_timer_post) -> (pre, post, fall_fast)"},
    {"derive_damage_hitlag_sdi_reset_post_mask", msl_derive_damage_hitlag_sdi_reset_post_mask_py,
     METH_VARARGS,
     "derive_damage_hitlag_sdi_reset_post_mask(action, hitlag, flags, pos_x, pos_y, stick_x, "
     "stick_y, damage_actions, sdi_step_mul) -> bool[:]"},
    {"derive_damage_entry_tilt_timer_reset_post_mask",
     msl_derive_damage_entry_tilt_timer_reset_post_mask_py, METH_VARARGS,
     "derive_damage_entry_tilt_timer_reset_post_mask(action, frame, hitlag, percent, "
     "instance_hit_by, damage_actions) -> bool[:]"},
    {"derive_guard_reflect_timer_plus1", msl_derive_guard_reflect_timer_plus1_py, METH_VARARGS,
     "derive_guard_reflect_timer_plus1(action, hitlag, act_guard_reflect, init_frames) -> "
     "uint8[:]"},
    {"derive_guard_reflect_origin_guardon", msl_derive_guard_reflect_origin_guardon_py,
     METH_VARARGS,
     "derive_guard_reflect_origin_guardon(action, act_guard_reflect, act_guard_on, act_guard) -> "
     "uint8[:]"},
    {"derive_grab_mash_stick_sign_post", msl_derive_grab_mash_stick_sign_post_py, METH_VARARGS,
     "derive_grab_mash_stick_sign_post(stick_x, stick_y, threshold) -> (x_sign, y_sign)"},
    {"derive_guard_release_lockout_and_lightshield",
     msl_derive_guard_release_lockout_and_lightshield_py, METH_VARARGS,
     "derive_guard_release_lockout_and_lightshield(action_id, shield_hp, hitlag, buttons_held, "
     "trigger_unit, button_mask_lr, button_mask_z, trigger_deadzone, guard_x10_init_frames, "
     "act_guard_on, act_guard, act_guard_reflect, act_guard_set_off) -> (xc, x10, light)"},
    {"derive_guard_special_enable_timer_x1c", msl_derive_guard_special_enable_timer_x1c_py,
     METH_VARARGS,
     "derive_guard_special_enable_timer_x1c(action, hitlag, flags, init, guard_on, guard, "
     "guard_off, guard_reflect, guard_set_off) -> uint8[:]"},
    {"derive_guard_setoff_hitlag_damage_min", msl_derive_guard_setoff_hitlag_damage_min_py,
     METH_VARARGS,
     "derive_guard_setoff_hitlag_damage_min(action, frame, hitlag, mul, base, guard_set_off) -> "
     "uint8[:]"},
    {"derive_guard_setoff_hitlag_exit_phase", msl_derive_guard_setoff_hitlag_exit_phase_py,
     METH_VARARGS,
     "derive_guard_setoff_hitlag_exit_phase(action, hitlag, guard_set_off) -> uint8[:]"},
    {"derive_guard_setoff_post_hitlag_owner", msl_derive_guard_setoff_post_hitlag_owner_py,
     METH_VARARGS,
     "derive_guard_setoff_post_hitlag_owner(action, phase, flags_221c, guard_set_off) -> uint8[:]"},
    {"derive_run_x0", msl_derive_run_x0_py, METH_VARARGS,
     "derive_run_x0(action, hitlag, init, run, run_direct, turn_run) -> uint8[:]"},
    {"derive_runbrake_cmd0", msl_derive_runbrake_cmd0_py, METH_VARARGS,
     "derive_runbrake_cmd0(action, anim_frame, char_id, on_by_char, off_by_char, run_brake) -> "
     "uint8[:]"},
    {"derive_dash_x4", msl_derive_dash_x4_py, METH_VARARGS,
     "derive_dash_x4(action, action_frame, dash, turn) -> uint8[:]"},
    {"derive_ecb_lock_timer", msl_derive_ecb_lock_timer_py, METH_VARARGS,
     "derive_ecb_lock_timer(on_ground, action, lock_frames, jump_f, jump_b, aerial_f, aerial_b) -> "
     "uint8[:]"},
    {"derive_ecb_lock_bottom_rel_y", msl_derive_ecb_lock_bottom_rel_y_py, METH_VARARGS,
     "derive_ecb_lock_bottom_rel_y(char, action, anim, anim_frame, on_ground, lock_timer) -> "
     "(float32[:], uint8[:])"},
    {"derive_damage_hitlag_colldata_ecb", msl_derive_damage_hitlag_colldata_ecb_py, METH_VARARGS,
     "derive_damage_hitlag_colldata_ecb(char, action, anim, anim_frame, rate, facing, ground, "
     "hitlag) -> (bottom, top, left, right, side, valid)"},
    {"derive_turn_internals", msl_derive_turn_internals_py, METH_VARARGS,
     "derive_turn_internals(action, frame, facing, stick_x, tilt_x, dash_abs, dash_max, "
     "turn_frames, turn, turn_run) -> (frames, has_turned, x8)"},
    {"compute_press_timer_u8", msl_compute_press_timer_u8_py, METH_VARARGS,
     "compute_press_timer_u8(buttons_pressed, press_mask, start_timer) -> uint8[:]"},
    {"compute_lr_press_timer_x67f", msl_compute_lr_press_timer_x67f_py, METH_VARARGS,
     "compute_lr_press_timer_x67f(buttons, trigger, hitlag_or_None, deadzone, lr_mask, z_mask, "
     "start_timer) -> uint8[:]"},
    {"compute_x672_trigger_timer_pre_post", msl_compute_x672_trigger_timer_pre_post_py,
     METH_VARARGS,
     "compute_x672_trigger_timer_pre_post(trigger, prev_or_None, min, guard_or_None, start) -> "
     "(pre, post)"},
    {"derive_downwait_timer", msl_derive_downwait_timer_py, METH_VARARGS,
     "derive_downwait_timer(action, hitstun_or_None, frames, down_damage_u, down_damage_d, "
     "down_wait_u, down_wait_d) -> int16[:]"},
    {"derive_damage_jump_buffer_x14", msl_derive_damage_jump_buffer_x14_py, METH_VARARGS,
     "derive_damage_jump_buffer_x14(action, hitstun, buttons_pressed, stick_y, tilt_y, "
     "hitlag_or_None, tap_threshold, tilt_max, xy_mask, damage_actions) -> uint16[:]"},
    {"derive_damage_meteor_cancel_x1a", msl_derive_damage_meteor_cancel_x1a_py, METH_VARARGS,
     "derive_damage_meteor_cancel_x1a(action, hitstun, source_angle, angle_min, angle_max, "
     "damage_actions) -> uint8[:]"},
    {"derive_damage_post_hitlag_cb_kind", msl_derive_damage_post_hitlag_cb_kind_py, METH_VARARGS,
     "derive_damage_post_hitlag_cb_kind(action, hitstun, damage_actions) -> uint8[:]"},
    {"derive_guard_tilt_state", msl_derive_guard_tilt_state_py, METH_VARARGS,
     "derive_guard_tilt_state(stick_x, stick_y, facing, action, frame, neutral, frame_max, lerp, "
     "guard_on, guard, guard_reflect) -> (x8, x4)"},
    {"derive_shine_release_state", msl_derive_shine_release_state_py, METH_VARARGS,
     "derive_shine_release_state(action, frame, held, hitlag, lag_init, b_mask, shine actions...) "
     "-> "
     "(release_lag, is_release)"},
    {"derive_kneebend_internals", msl_derive_kneebend_internals_py, METH_VARARGS,
     "derive_kneebend_internals(action, buttons, pressed, stick_y, cstick_y, tilt_y, thresholds, "
     "actions...) -> (jump_input, is_short_hop)"},
    {"derive_smash_charge_seed_lanes", msl_derive_smash_charge_seed_lanes_py, METH_VARARGS,
     "derive_smash_charge_seed_lanes(char, action, anim, frame_speed, ground, hitlag, hitstun, "
     "buttons, a_mask) -> (state,frames,hold,saved_rate_q16)"},
    {"derive_magnify_damage_counter_x1910", msl_derive_magnify_damage_counter_x1910_py,
     METH_VARARGS,
     "derive_magnify_damage_counter_x1910(action, flags, inside, percent, optional contact lanes, "
     "interval, limit, amount) -> uint16[:]"},
    {"derive_colanim_internals", msl_derive_colanim_internals_py, METH_VARARGS,
     "derive_colanim_internals(action, frame, hitlag, hitstun, hurtbox, timers, action sets) -> "
     "(x198c, x1990, x1994, x2221_b0, rebirth_fall_x1994)"},
    {"derive_capture_mash_buttons_pressed", msl_derive_capture_mash_buttons_pressed_py,
     METH_VARARGS,
     "derive_capture_mash_buttons_pressed(buttons, l, r, deadzone, a_mask, z_mask, lr_mask) -> "
     "uint16[:, :]"},
    {"derive_capture_grab_hidden_post", msl_derive_capture_grab_hidden_post_py, METH_VARARGS,
     "derive_capture_grab_hidden_post(action, frame, owner, percent, buttons, sticks, frame_speed, "
     "mash signs, constants...) -> hidden capture lanes"},
    {"derive_ledge_cooldown", msl_derive_ledge_cooldown_py, METH_VARARGS,
     "derive_ledge_cooldown(action, hitlag, cooldown_frames) -> uint8[:]"},
    {"derive_cliff_ledge_floor_segment_id", msl_derive_cliff_ledge_floor_segment_id_py,
     METH_VARARGS,
     "derive_cliff_ledge_floor_segment_id(action, facing, on_ground, cooldown, left_floor, "
     "right_floor) -> uint16[:]"},
    {"derive_cliff_option_stick_latch_x8", msl_derive_cliff_option_stick_latch_x8_py, METH_VARARGS,
     "derive_cliff_option_stick_latch_x8(action, main_x, main_y, c_x, c_y, deadzones, "
     "threshold) -> uint8[:]"},
    {"derive_match_flow_timer", msl_derive_match_flow_timer_py, METH_VARARGS,
     "derive_match_flow_timer(action, port0, common timers...) -> uint8[:]"},
    {"derive_passivewall_timer", msl_derive_passivewall_timer_py, METH_VARARGS,
     "derive_passivewall_timer(action, frame, total_frames) -> uint8[:]"},
    {"derive_walljump_phase_seed_lanes", msl_derive_walljump_phase_seed_lanes_py, METH_VARARGS,
     "derive_walljump_phase_seed_lanes(action, frame, setup_x_delta, pos_x, pos_y, raw_main_x) -> "
     "(timer, side)"},
    {"derive_entry_end_fall_lock", msl_derive_entry_end_fall_lock_py, METH_VARARGS,
     "derive_entry_end_fall_lock(action, on_ground, entry_end, fall) -> uint8[:]"},
    {"derive_jab_rapid_count", msl_derive_jab_rapid_count_py, METH_VARARGS,
     "derive_jab_rapid_count(action, buttons_released, buttons_pressed, a_mask) -> uint8[:]"},
    {"derive_walk_anim_source_vel", msl_derive_walk_anim_source_vel_py, METH_VARARGS,
     "derive_walk_anim_source_vel(action, char, facing_dir1, frame_speed, divisor LUTs) -> "
     "float32[:]"},
    {"derive_walk_retarget_tick_source_vel", msl_derive_walk_retarget_tick_source_vel_py,
     METH_VARARGS,
     "derive_walk_retarget_tick_source_vel(action, char, facing, anim, ref_af, velocities, LUTs) "
     "-> float32[:]"},
    {"derive_run_anim_source_vel", msl_derive_run_anim_source_vel_py, METH_VARARGS,
     "derive_run_anim_source_vel(action, char, facing_dir1, frame_speed, scaling LUT) -> "
     "float32[:]"},
    {"derive_facing_dir1_sign", msl_derive_facing_dir1_sign_py, METH_VARARGS,
     "derive_facing_dir1_sign(facing, action) -> int8[:]"},
    {"derive_specialhi_rotate_model_seed_lane", msl_derive_specialhi_rotate_model_seed_lane_py,
     METH_VARARGS, "derive_specialhi_rotate_model_seed_lane(...) -> (angle, valid)"},
    {"derive_throw_pulse_seed_lanes", msl_derive_throw_pulse_seed_lanes_py, METH_VARARGS,
     "derive_throw_pulse_seed_lanes(...) -> (consumed,crossed_prev,pending)"},
    {"derive_throw_laser_item_hitlist_seed_lanes",
     msl_derive_throw_laser_item_hitlist_seed_lanes_py, METH_VARARGS,
     "derive_throw_laser_item_hitlist_seed_lanes(...) -> "
     "(victim_port,victim_cd,victim_hitbox_mask,victim_iid)"},
    {"derive_item_attack_fields", msl_derive_item_attack_fields_py, METH_VARARGS,
     "derive_item_attack_fields(item fields, fighter attack fields, players[, "
     "prev_frame_spawn_kinds]) -> "
     "(attack_id,attack_instance)"},
    {"derive_item_reflect_damage_mul", msl_derive_item_reflect_damage_mul_py, METH_VARARGS,
     "derive_item_reflect_damage_mul(item fields, fighter fields, powershield_mul, players) -> "
     "float32[:,slots]"},
    {"derive_item_hidden_callback_seed_lanes", msl_derive_item_hidden_callback_seed_lanes_py,
     METH_VARARGS,
     "derive_item_hidden_callback_seed_lanes(seed/ref item fields, action fields, laser LUT) -> "
     "item hidden callback arrays"},
    {"derive_yoshi_shyguy_seed_lanes", msl_derive_yoshi_shyguy_seed_lanes_py, METH_VARARGS,
     "derive_yoshi_shyguy_seed_lanes(item fields, params...) -> Shy Guy seed lanes"},
    {"derive_dream_whispy_wind_seed_lanes", msl_derive_dream_whispy_wind_seed_lanes_py,
     METH_VARARGS,
     "derive_dream_whispy_wind_seed_lanes(seed/input/ref bytes, players, stage, speed, eps) -> "
     "(dir,valid,timer)"},
    {"derive_illusion_seed_position_updates", msl_derive_illusion_seed_position_updates_py,
     METH_VARARGS,
     "derive_illusion_seed_position_updates(item fields, fighter fields, illusion LUT) -> "
     "(mask,pos_x,pos_y)"},
    {"trim_stale_hitlist_seed_bridge", msl_trim_stale_hitlist_seed_bridge_py, METH_VARARGS,
     "trim_stale_hitlist_seed_bridge(hitlist arrays, replay fields, constants) -> None"},
    {"derive_attacker_shield_ground_kb_vel", msl_derive_attacker_shield_ground_kb_vel_py,
     METH_VARARGS,
     "derive_attacker_shield_ground_kb_vel(replay fields, LUTs, constants) -> float32[:,4]"},
    {"derive_guardsetoff_frame_speed_overrides", msl_derive_guardsetoff_frame_speed_overrides_py,
     METH_VARARGS,
     "derive_guardsetoff_frame_speed_overrides(replay fields, LUTs, constants) -> float32[:,4]"},
    {"derive_shield_contact_seed_bridge", msl_derive_shield_contact_seed_bridge_py, METH_VARARGS,
     "derive_shield_contact_seed_bridge(hitlist arrays, replay fields, LUTs, constants) -> "
     "(shield_hit_int_damage, shield_damage_taken)"},
    {"derive_rebound_seed_lanes", msl_derive_rebound_seed_lanes_py, METH_VARARGS,
     "derive_rebound_seed_lanes(replay fields, speeds, constants) -> (ground_accel_2, anim_rate)"},
    {"derive_mpcoll_wall_seed_lanes", msl_derive_mpcoll_wall_seed_lanes_py, METH_VARARGS,
     "derive_mpcoll_wall_seed_lanes(action, frame, hitlag, hitstun, pos_x, pos_y, stage, "
     "segment arrays...) -> (kind, wall_id)"},
    {"derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes", msl_derive_source_clear_timer_py,
     METH_VARARGS,
     "derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(...) -> (timer, phase)"},
    {"derive_source_clear_grounded_damage_clear_phase_seed_lane",
     msl_derive_source_clear_grounded_damage_clear_phase_py, METH_VARARGS,
     "derive_source_clear_grounded_damage_clear_phase_seed_lane(...) -> uint8[:]"},
    {"derive_source_clear_terminal_phase_seed_lane", msl_derive_source_clear_terminal_phase_py,
     METH_VARARGS, "derive_source_clear_terminal_phase_seed_lane(...) -> uint8[:]"},
    {"derive_fighter_8006cda4_pre_gate_consume_count",
     msl_derive_fighter_8006cda4_pre_gate_count_py, METH_VARARGS,
     "derive_fighter_8006cda4_pre_gate_consume_count(...) -> uint8[:]"},
    {"derive_source_clear_processhit_damage_pending_phase_seed_lane",
     msl_derive_source_clear_processhit_damage_pending_phase_py, METH_VARARGS,
     "derive_source_clear_processhit_damage_pending_phase_seed_lane(action, flags) -> uint8[:]"},
    {"derive_phantom_damage_pending_seed_lanes", msl_derive_phantom_damage_pending_seed_lanes_py,
     METH_VARARGS,
     "derive_phantom_damage_pending_seed_lanes(percent, hitlag, action, hit_by, iid, players) -> "
     "(damage,timer,source)"},
    {"derive_grounded_overlap_hidden_pos_z", msl_derive_grounded_overlap_hidden_pos_z_py,
     METH_VARARGS,
     "derive_grounded_overlap_hidden_pos_z(num_players, char, action, ground, stocks, pos_x, "
     "pos_z, facing, push_x_lut, push_y_lut, step, z_max) -> float32[:, :]"},
    {"compute_fighter_stick_input_counters", msl_compute_fighter_stick_input_counters_py,
     METH_VARARGS,
     "compute_fighter_stick_input_counters(stick_x, stick_y, tilt_thresh_x, tilt_thresh_y, "
     "start_timer) -> seven uint8 arrays"},
    {"compute_fighter_trigger_input_counters", msl_compute_fighter_trigger_input_counters_py,
     METH_VARARGS,
     "compute_fighter_trigger_input_counters(trigger_unit, trigger_min, start_timer) -> three "
     "uint8 arrays"},
    {"compute_fighter_button_timers", msl_compute_fighter_button_timers_py, METH_VARARGS,
     "compute_fighter_button_timers(buttons_pressed, hitlag_or_None, masks..., start_timer) -> "
     "eight uint8 arrays"},
    {"derive_illusion_ghost_pos012", msl_derive_illusion_ghost_pos012_py, METH_VARARGS,
     "derive_illusion_ghost_pos012(action_id, action_frame, pos_x, pos_y) -> six float32 arrays"},
    {"derive_combat_hitlist_seed_fields", msl_derive_combat_hitlist_seed_fields_py, METH_VARARGS,
     "derive_combat_hitlist_seed_fields(...) -> (cd,iid,hb_valid,hb_cd,hb_iid,shield_kind)"},
    {"ecb_bottom_rel_y", msl_ecb_bottom_rel_y_py, METH_VARARGS,
     "ecb_bottom_rel_y(char_id, animation_index, action_frame) -> float"},
    {"ecb_extents_rel", msl_ecb_extents_rel_py, METH_VARARGS,
     "ecb_extents_rel(char_id, animation_index, action_frame) -> (min_x, max_x, min_y, max_y)"},
    {"anim_pose_matrix", msl_anim_pose_matrix_py, METH_VARARGS,
     "anim_pose_matrix(char_id, msid, frame, part_id) -> np.ndarray[float32] shape=(12,)"},
    {"move_tables_debug_query", msl_move_tables_debug_query_py, METH_VARARGS,
     "move_tables_debug_query(kind, char_id, action_or_msid, a, b) -> test helper"},
    {"move_tables_throw_has_release", msl_move_tables_throw_has_release_py, METH_VARARGS,
     "move_tables_throw_has_release(char_id, throw_action_id) -> 0/1"},
    {"move_tables_throw_release_frame", msl_move_tables_throw_release_frame_py, METH_VARARGS,
     "move_tables_throw_release_frame(char_id, throw_action_id) -> (ok, release_af)"},
    {"move_tables_throw_release_hit_idx", msl_move_tables_throw_release_hit_idx_py, METH_VARARGS,
     "move_tables_throw_release_hit_idx(char_id, throw_action_id, cur_anim_frame) -> (released, "
     "hit_idx)"},
    {"move_tables_throw_hitbox_params", msl_move_tables_throw_hitbox_params_py, METH_VARARGS,
     "move_tables_throw_hitbox_params(char_id, throw_action_id, hit_idx) -> "
     "(damage, angle, kbg, wsk, bkb, element, sfx_kind, sfx_severity) or None"},
    {"move_tables_throw_cmd1_active", msl_move_tables_throw_cmd1_active_py, METH_VARARGS,
     "move_tables_throw_cmd1_active(char_id, throw_action_id, cur_anim_frame) -> 0/1"},
    {"move_tables_throw_should_spawn_projectile", msl_move_tables_throw_should_spawn_projectile_py,
     METH_VARARGS,
     "move_tables_throw_should_spawn_projectile(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> 0/1"},
    {"move_tables_throw_should_flip_facing", msl_move_tables_throw_should_flip_facing_py,
     METH_VARARGS,
     "move_tables_throw_should_flip_facing(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> 0/1"},
    {"move_tables_throw_crossed_projectile_pulse_frame",
     msl_move_tables_throw_crossed_projectile_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_crossed_projectile_pulse_frame(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> (ok, pulse_frame)"},
    {"move_tables_throw_projectile_first_pulse_frame",
     msl_move_tables_throw_projectile_first_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_projectile_first_pulse_frame(char_id, throw_action_id) -> "
     "(ok, pulse_frame)"},
    {"move_tables_throw_projectile_last_pulse_frame",
     msl_move_tables_throw_projectile_last_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_projectile_last_pulse_frame(char_id, throw_action_id) -> "
     "(ok, pulse_frame)"},
    {"move_tables_throw_projectile_pulse_ordinal",
     msl_move_tables_throw_projectile_pulse_ordinal_py, METH_VARARGS,
     "move_tables_throw_projectile_pulse_ordinal(char_id, throw_action_id, pulse_frame) -> "
     "(ok, ordinal)"},
    {"move_tables_special_pseudo_random_sfx_ranges_crossed",
     msl_move_tables_special_pseudo_random_sfx_ranges_crossed_py, METH_VARARGS,
     "move_tables_special_pseudo_random_sfx_ranges_crossed(char_id, msid, prev_anim_frame, "
     "cur_anim_frame, max_out=8) -> tuple[int, ...]"},
    {"hurtcaps_world", msl_hurtcaps_world_py, METH_VARARGS,
     "hurtcaps_world(handle, batch_index, player_index) -> (caps[MSL_MAX_HURTCAPS,7], count)"},
    {"hitboxes_world", msl_hitboxes_world_py, METH_VARARGS,
     "hitboxes_world(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,10], count)"},
    {"hitboxes_world_full", msl_hitboxes_world_full_py, METH_VARARGS,
     "hitboxes_world_full(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,16], "
     "count)"},
    {"debug_combat_contacts", msl_debug_combat_contacts_py, METH_VARARGS,
     "debug_combat_contacts(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_hitlist_fighter_contains", msl_debug_hitlist_fighter_contains_py, METH_VARARGS,
     "debug_hitlist_fighter_contains(handle, batch_index, attacker, hb_id, victim) -> 0/1"},
    {"debug_hitlist_fighter_capsule", msl_debug_hitlist_fighter_capsule_py, METH_VARARGS,
     "debug_hitlist_fighter_capsule(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitlistCapsule)]"},
    {"debug_combat_contacts_filtered", msl_debug_combat_contacts_filtered_py, METH_VARARGS,
     "debug_combat_contacts_filtered(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_combat_select_body_hits", msl_debug_combat_select_body_hits_py, METH_VARARGS,
     "debug_combat_select_body_hits(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_combat_contacts_classified", msl_debug_combat_contacts_classified_py, METH_VARARGS,
     "debug_combat_contacts_classified(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContactClassified)], count)"},
    {"debug_combat_contacts_classified_filtered", msl_debug_combat_contacts_classified_filtered_py,
     METH_VARARGS,
     "debug_combat_contacts_classified_filtered(handle, batch_index, max_contacts=256) -> "
     "(bytes[max, sizeof(MslDebugCombatContactClassified)], count)"},
    {"debug_shield_candidate_decisions", msl_debug_shield_candidate_decisions_py, METH_VARARGS,
     "debug_shield_candidate_decisions(handle, batch_index, max_rows=256) -> "
     "(bytes[max, sizeof(MslDebugShieldCandidateDecision)], count)"},
    {"debug_shield_bubbles_world", msl_debug_shield_bubbles_world_py, METH_VARARGS,
     "debug_shield_bubbles_world(handle, batch_index) -> np.ndarray[float32] "
     "shape=(MSL_MAX_PLAYERS,4)"},
    {"debug_clear_hitboxes_world", msl_debug_clear_hitboxes_world_py, METH_VARARGS,
     "debug_clear_hitboxes_world(handle, batch_index, player_index)"},
    {"debug_set_hitbox_world", msl_debug_set_hitbox_world_py, METH_VARARGS,
     "debug_set_hitbox_world(handle, batch_index, player_index, hitbox_id, x,y,z,radius,damage, "
     "enabled=1)"},
    {"debug_set_hitbox_flags", msl_debug_set_hitbox_flags_py, METH_VARARGS,
     "debug_set_hitbox_flags(handle, batch_index, player_index, hitbox_id, hitbox_flags_u16)"},
    {"debug_set_hitbox_group", msl_debug_set_hitbox_group_py, METH_VARARGS,
     "debug_set_hitbox_group(handle, batch_index, player_index, hitbox_id, hit_group_0_7)"},
    {"debug_set_hitbox_enable_edge", msl_debug_set_hitbox_enable_edge_py, METH_VARARGS,
     "debug_set_hitbox_enable_edge(handle, batch_index, player_index, hitbox_id, enable_edge_u8)"},
    {"debug_set_hitbox_element", msl_debug_set_hitbox_element_py, METH_VARARGS,
     "debug_set_hitbox_element(handle, batch_index, player_index, hitbox_id, element_u8)"},
    {"debug_set_hitbox_kb_params", msl_debug_set_hitbox_kb_params_py, METH_VARARGS,
     "debug_set_hitbox_kb_params(handle, batch_index, player_index, hitbox_id, angle_deg_u16, "
     "kbg_u16, wsk_u16, bkb_u16)"},
    {"debug_clear_hurtcaps_world", msl_debug_clear_hurtcaps_world_py, METH_VARARGS,
     "debug_clear_hurtcaps_world(handle, batch_index, player_index)"},
    {"debug_set_hurtcap_world", msl_debug_set_hurtcap_world_py, METH_VARARGS,
     "debug_set_hurtcap_world(handle, batch_index, player_index, hurtcap_id, "
     "ax,ay,az,bx,by,bz,radius)"},
    {"debug_set_hurtcap_height", msl_debug_set_hurtcap_height_py, METH_VARARGS,
     "debug_set_hurtcap_height(handle, batch_index, player_index, hurtcap_id, height_u8)"},
    {"debug_set_hurtcap_enabled", msl_debug_set_hurtcap_enabled_py, METH_VARARGS,
     "debug_set_hurtcap_enabled(handle, batch_index, player_index, hurtcap_id, enabled=0/1)"},
    {"debug_combat_resolve", msl_debug_combat_resolve_py, METH_VARARGS,
     "debug_combat_resolve(handle) -> run combat_resolve() only"},
    {"debug_set_hitlag", msl_debug_set_hitlag_py, METH_VARARGS,
     "debug_set_hitlag(handle, batch_index, player_index, hitlag_frames_u16)"},
    {"debug_set_damage_source", msl_debug_set_damage_source_py, METH_VARARGS,
     "debug_set_damage_source(handle, batch_index, player_index, last_hit_by_u8, "
     "instance_hit_by_u16)"},
    {"debug_set_damage_phase", msl_debug_set_damage_phase_py, METH_VARARGS,
     "debug_set_damage_phase(handle, batch_index, player_index, action_id_u16, hitstun_u16, "
     "damage_time_since_hit_i16, on_ground_u8)"},
    {"debug_set_phantom_damage", msl_debug_set_phantom_damage_py, METH_VARARGS,
     "debug_set_phantom_damage(handle, batch_index, player_index, pending_damage_f32, timer_u16, "
     "source_slot_u8)"},
    {"debug_set_prev_action_id", msl_debug_set_prev_action_id_py, METH_VARARGS,
     "debug_set_prev_action_id(handle, batch_index, player_index, prev_action_id_u16)"},
    {"debug_set_smash_charge_state", msl_debug_set_smash_charge_state_py, METH_VARARGS,
     "debug_set_smash_charge_state(handle, batch_index, player_index, state_u8, frames_u8, "
     "hold_frames_max_u8)"},
    {"debug_set_hit_status_override", msl_debug_set_hit_status_override_py, METH_VARARGS,
     "debug_set_hit_status_override(handle, batch_index, player_index, status_i32)"},
    {"debug_point_segment_dist2", msl_debug_point_segment_dist2_py, METH_VARARGS,
     "debug_point_segment_dist2(px,py,pz, ax,ay,az, bx,by,bz) -> (dist2, t)"},
    {"anim_bake_ssanim01", msl_anim_bake_ssanim01_py, METH_VARARGS,
     "anim_bake_ssanim01(rest_rot, rest_pos, rest_scl, parent_part, part_flags, order, "
     "local_parts, joint_parts, "
     "update_parts, fobj_starts, fobj_desc, ad_source, frame_count, inv_scale_part, "
     "inv_model_scale, end_frame, aobj_loop) -> "
     "(mats_bytes, locals_bytes, transn_bytes)"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "melee_sim._native", NULL, -1, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit__native(void) {
  import_array();
  return PyModule_Create(&moduledef);
}
