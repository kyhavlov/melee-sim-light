#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <numpy/arrayobject.h>

#include "../src/alloc.h"
#include "../src/api.h"
#include "../src/anim_table.h"
#include "../src/anim_pose.h"
#include "../src/char_params.h"
#include "../src/common_params.h"
#include "../src/ecb_tables.h"
#include "../src/hitboxes_tables.h"
#include "../src/hurtcaps_tables.h"
#include "../src/hitlist.h"
#include "../src/item_article_params.h"
#include "../src/move_tables.h"
#include "../src/shield_tilt_table.h"
#include "../src/specialhi_pose.h"
#include "../src/stage_collision.h"

typedef struct {
  MslBatch* batch;
} PyMslHandle;

static void pymsl_capsule_destructor(PyObject* capsule) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return;
  }
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h);
}

static PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim,
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

static int require_exact_2d_shape(PyArrayObject* arr, npy_intp rows, npy_intp cols,
                                  const char* name) {
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
    PyErr_SetString(PyExc_MemoryError, "msl_batch_create failed");
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

  PyMslHandle* h = (PyMslHandle*)PyMem_Malloc(sizeof(PyMslHandle));
  if (h == NULL) {
    msl_batch_destroy(batch);
    PyErr_NoMemory();
    return NULL;
  }
  h->batch = batch;

  PyObject* capsule = PyCapsule_New(h, "msl.Handle", pymsl_capsule_destructor);
  if (capsule == NULL) {
    // PyCapsule_New sets an exception on failure. We must clean up manually here because the capsule
    // was never created (calling the capsule destructor with NULL would be a bug).
    msl_batch_destroy(h->batch);
    h->batch = NULL;
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

static PyObject* msl_write_rl_observation(PyObject* self, PyObject* args) {
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
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslRlObservation)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslRlObservation");
    return NULL;
  }

  const uint8_t* viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
  const size_t viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);
  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t out_stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_rl_observation(h->batch, viewpoint_bytes, viewpoint_stride,
                                                 out_bytes, out_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_rl_observation failed: %d", err);
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
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}", "seed", (int)sizeof(MslSeed),
                       "match_config", (int)sizeof(MslMatchConfig), "input", (int)sizeof(MslInput),
                       "compare", (int)sizeof(MslCompare), "sample", (int)sizeof(MslSample),
                       "rl_observation", (int)sizeof(MslRlObservation), "terminal",
                       (int)sizeof(MslTerminal), "processed_input", (int)sizeof(MslProcessedInput),
                       "internals", (int)sizeof(MslDebugInternals), "collision_contacts",
                       (int)sizeof(MslDebugCollisionContacts));
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
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph((uint32_t)stage_id_u);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx = stage_collision_floor_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  const MslStageFloorLine* line = &graph->lines[(size_t)idx];
  return Py_BuildValue("{s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:i}", "segment_i", (int)line->segment_i, "x0",
                       (double)line->x0, "y0", (double)line->y0, "x1", (double)line->x1, "y1",
                       (double)line->y1, "is_ledge", (int)line->is_ledge, "is_platform",
                       (int)line->is_platform, "line_index", idx);
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

static PyObject* msl_derive_camera_target_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOO", &char_obj, &anim_obj, &anim_frame_obj, &scale_y_obj,
                        &facing_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj)) {
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  if (char_id == NULL) {
    return NULL;
  }
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 1, "animation_index_u32");
  if (anim == NULL) {
    return NULL;
  }
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  if (anim_frame == NULL) {
    return NULL;
  }
  PyArrayObject* scale_y =
      require_contiguous_array(scale_y_obj, NPY_FLOAT32, 1, "fighter_scale_y_f32");
  if (scale_y == NULL) {
    return NULL;
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  if (facing == NULL) {
    return NULL;
  }
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  if (pos_x == NULL) {
    return NULL;
  }
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  if (pos_y == NULL) {
    return NULL;
  }
  PyArrayObject* pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 1, "pos_z_f32");
  if (pos_z == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  if (PyArray_DIM(anim, 0) != n || PyArray_DIM(anim_frame, 0) != n ||
      PyArray_DIM(scale_y, 0) != n || PyArray_DIM(facing, 0) != n || PyArray_DIM(pos_x, 0) != n ||
      PyArray_DIM(pos_y, 0) != n || PyArray_DIM(pos_z, 0) != n) {
    PyErr_SetString(PyExc_ValueError, "camera target world derivation inputs must share length");
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "camera target native tables failed to initialize");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_r = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out_x == NULL || out_y == NULL || out_z == NULL || out_r == NULL) {
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    Py_XDECREF(out_r);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = (const float*)PyArray_DATA(pos_z);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);
  float* out_r_p = (float*)PyArray_DATA(out_r);

  for (npy_intp i = 0; i < n; i++) {
    const uint8_t cid = char_p[i];
    const MslCharParams* ch = msl_char_params(cid);
    if (ch == NULL) {
      continue;
    }
    const uint32_t anim_u32 = anim_p[i];
    if (anim_u32 > 0xFFFFu) {
      continue;
    }
    const float frame_f = anim_frame_p[i];
    if (!isfinite(frame_f)) {
      continue;
    }
    const int frame_i = (int)floorf(frame_f);
    if (frame_i < 0 || frame_i > 0xFFFF) {
      continue;
    }
    float m[12];
    if (anim_pose_get_matrix(cid, (uint16_t)anim_u32, (uint16_t)frame_i,
                             ch->camera_zoom_target_bone_part_id, m) != 0) {
      continue;
    }

    const float ox = ch->camera_zoom_target_offset_x;
    const float oy = ch->camera_zoom_target_offset_y;
    const float oz = ch->camera_zoom_target_offset_z;
    float lx = (float)(m[0] * ox + m[1] * oy + m[2] * oz + m[3]);
    float ly = (float)(m[4] * ox + m[5] * oy + m[6] * oz + m[7]);
    float lz = (float)(m[8] * ox + m[9] * oy + m[10] * oz + m[11]);

    float scale = scale_y_p[i];
    if (!isfinite(scale) || !(scale > 0.0f)) {
      scale = 1.0f;
    }
    float model_scaling = ch->model_scaling;
    if (!isfinite(model_scaling) || !(model_scaling > 0.0f)) {
      model_scaling = 1.0f;
    }
    const float pose_scale = (float)(scale * model_scaling);
    lx = (float)(lx * pose_scale);
    ly = (float)(ly * pose_scale);
    lz = (float)(lz * pose_scale);

    const float facing_dir = facing_p[i] ? 1.0f : -1.0f;
    out_x_p[i] = (float)(pos_x_p[i] + facing_dir * lz);
    out_y_p[i] = (float)(pos_y_p[i] + ly);
    out_z_p[i] = (float)(pos_z_p[i] - facing_dir * lx);
    out_r_p[i] = (float)(ch->camera_box_radius * scale);
  }

  return Py_BuildValue("NNNN", out_x, out_y, out_z, out_r);
}

static inline void msl_py_mtx34_mul_point(const float m[12], float x, float y, float z,
                                          float* out_x, float* out_y, float* out_z) {
  *out_x = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
  *out_y = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
  *out_z = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
}

static inline uint8_t msl_py_apply_specialhi_xrotn(uint8_t char_id, uint16_t action_id,
                                                   uint16_t msid, uint16_t frame, uint16_t part_id,
                                                   float model_scale, float rotate_model,
                                                   uint8_t rotate_model_valid, float* io_x,
                                                   float* io_y, float* io_z) {
  if (rotate_model_valid == 0u || !msl_specialhi_rotate_model_action(action_id) ||
      !msl_anim_part_under_xrotn(char_id, part_id) || !isfinite(rotate_model)) {
    return 0u;
  }
  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, 2u, m) != 0) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  msl_py_mtx34_mul_point(m, 0.0f, 0.0f, 0.0f, &ax0, &ay0, &az0);
  msl_py_mtx34_mul_point(m, 1.0f, 0.0f, 0.0f, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static PyObject* msl_derive_hitbox_prev_centers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* rotate_model_obj = NULL;
  PyObject* rotate_valid_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "iOOOOOOOOOOOO", &num_players, &char_obj, &action_obj, &anim_obj,
                        &action_frame_obj, &anim_frame_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj,
                        &facing_obj, &scale_y_obj, &rotate_model_obj, &rotate_valid_obj)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 2, "animation_index");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 2, "action_frame");
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "pos_y");
  PyArrayObject* pos_z = NULL;
  if (pos_z_obj != Py_None) {
    pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 2, "pos_z");
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 2, "facing");
  PyArrayObject* scale_y = require_contiguous_array(scale_y_obj, NPY_FLOAT32, 2, "fighter_scale_y");
  PyArrayObject* rotate_model = NULL;
  if (rotate_model_obj != Py_None) {
    rotate_model =
        require_contiguous_array(rotate_model_obj, NPY_FLOAT32, 2, "specialhi_rotate_model_f32");
  }
  PyArrayObject* rotate_valid = NULL;
  if (rotate_valid_obj != Py_None) {
    rotate_valid =
        require_contiguous_array(rotate_valid_obj, NPY_UINT8, 2, "specialhi_rotate_model_valid_u8");
  }
  if (char_id == NULL || action_id == NULL || anim == NULL || action_frame == NULL ||
      anim_frame == NULL || pos_x == NULL || pos_y == NULL ||
      (pos_z_obj != Py_None && pos_z == NULL) || facing == NULL || scale_y == NULL ||
      (rotate_model_obj != Py_None && rotate_model == NULL) ||
      (rotate_valid_obj != Py_None && rotate_valid == NULL)) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  const npy_intp width = PyArray_DIM(char_id, 1);
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "char_id width smaller than num_players");
    return NULL;
  }
  if (require_exact_2d_shape(action_id, n, width, "action_id") != 0 ||
      require_exact_2d_shape(anim, n, width, "animation_index") != 0 ||
      require_exact_2d_shape(action_frame, n, width, "action_frame") != 0 ||
      require_exact_2d_shape(anim_frame, n, width, "anim_frame_f32") != 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x") != 0 ||
      require_exact_2d_shape(pos_y, n, width, "pos_y") != 0 ||
      (pos_z != NULL && require_exact_2d_shape(pos_z, n, width, "pos_z") != 0) ||
      require_exact_2d_shape(facing, n, width, "facing") != 0 ||
      require_exact_2d_shape(scale_y, n, width, "fighter_scale_y") != 0 ||
      (rotate_model != NULL &&
       require_exact_2d_shape(rotate_model, n, width, "specialhi_rotate_model_f32") != 0) ||
      (rotate_valid != NULL &&
       require_exact_2d_shape(rotate_valid, n, width, "specialhi_rotate_model_valid_u8") != 0)) {
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0 || anim_table_init() != 0 ||
      hitboxes_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "hitbox prev center native tables failed to initialize");
    return NULL;
  }

  npy_intp dims_valid[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  npy_intp dims_xyz[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  PyArrayObject* out_valid = (PyArrayObject*)PyArray_ZEROS(3, dims_valid, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  if (out_valid == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    Py_XDECREF(out_valid);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const int16_t* action_frame_p = (const int16_t*)PyArray_DATA(action_frame);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = pos_z != NULL ? (const float*)PyArray_DATA(pos_z) : NULL;
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const float* rotate_model_p =
      rotate_model != NULL ? (const float*)PyArray_DATA(rotate_model) : NULL;
  const uint8_t* rotate_valid_p =
      rotate_valid != NULL ? (const uint8_t*)PyArray_DATA(rotate_valid) : NULL;
  uint8_t* valid_p = (uint8_t*)PyArray_DATA(out_valid);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);

  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      if (action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      const float af = anim_frame_p[pi];
      if (!isfinite(af) || af < 0.0f) {
        continue;
      }
      const uint16_t frame = (uint16_t)floorf(af);
      const uint8_t cid = char_p[pi];
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }
      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(cid, (uint16_t)anim_p[pi], &events, &event_count) != 0 ||
          events == NULL || event_count == 0) {
        continue;
      }
      const MslHitboxEvent* active[MSL_MAX_HITBOXES] = {0};
      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        if (ev->frame > frame) {
          continue;
        }
        if (ev->kind == 1u) {
          if (ev->hitbox_id == 0xFFu) {
            memset(active, 0, sizeof(active));
          } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
            active[ev->hitbox_id] = NULL;
          }
        } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
          active[ev->hitbox_id] = ev;
        }
      }

      const float scale_y_val = scale_y_p[pi];
      const float model_scaling =
          (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
      const float model_scale = (float)(scale_y_val * model_scaling);
      const float facing_dir = facing_p[pi] ? 1.0f : -1.0f;
      const float px = pos_x_p[pi];
      const float py = pos_y_p[pi];
      const float pz = pos_z_p != NULL ? pos_z_p[pi] : 0.0f;
      const uint16_t action = action_p[pi];
      const float rotate_model_val = rotate_model_p != NULL ? rotate_model_p[pi] : 0.0f;
      const uint8_t rotate_valid_val = rotate_valid_p != NULL ? rotate_valid_p[pi] : 0u;

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const MslHitboxEvent* ev = active[hb_id];
        if (ev == NULL) {
          continue;
        }
        float m[12];
        if (anim_pose_get_matrix(cid, (uint16_t)anim_p[pi], frame, ev->bone_part_id, m) != 0) {
          continue;
        }
        float lx = 0.0f, ly = 0.0f, lz = 0.0f;
        msl_py_mtx34_mul_point(m, ev->x, ev->y, ev->z, &lx, &ly, &lz);
        lx = (float)(lx * model_scale);
        ly = (float)(ly * model_scale);
        lz = (float)(lz * model_scale);
        (void)msl_py_apply_specialhi_xrotn(cid, action, (uint16_t)anim_p[pi], frame,
                                           ev->bone_part_id, model_scale, rotate_model_val,
                                           rotate_valid_val, &lx, &ly, &lz);
        const npy_intp oi =
            (fi * (npy_intp)MSL_MAX_PLAYERS + p) * (npy_intp)MSL_MAX_HITBOXES + hb_id;
        valid_p[oi] = 1u;
        out_x_p[oi] = (float)(facing_dir * lz + px);
        out_y_p[oi] = (float)(ly + py);
        out_z_p[oi] = (float)(-facing_dir * lx + pz);
      }
    }
  }

  return Py_BuildValue("NNNN", out_valid, out_x, out_y, out_z);
}

static PyObject* msl_derive_combo_push_timer_seed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* combo_count_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* victim_obj = Py_None;
  if (!PyArg_ParseTuple(args, "OO|O", &combo_count_obj, &last_attack_obj, &victim_obj)) {
    return NULL;
  }
  PyArrayObject* counts = require_contiguous_array(combo_count_obj, NPY_UINT8, 2, "combo_count");
  PyArrayObject* attacks =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 2, "last_attack_landed");
  PyArrayObject* victims = NULL;
  if (victim_obj != Py_None) {
    victims = require_contiguous_array(victim_obj, NPY_UINT8, 2, "combo_victim_port");
  }
  if (counts == NULL || attacks == NULL || (victim_obj != Py_None && victims == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(counts, 0);
  const npy_intp count_w = PyArray_DIM(counts, 1);
  if (count_w > (npy_intp)MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "combo_count width exceeds MSL_MAX_PLAYERS");
    return NULL;
  }
  if (require_exact_2d_shape(attacks, n, count_w, "last_attack_landed") != 0 ||
      (victims != NULL && require_exact_2d_shape(victims, n, count_w, "combo_victim_port") != 0)) {
    return NULL;
  }
  if (common_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "common_params_init failed");
    return NULL;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "common params unavailable");
    return NULL;
  }

  npy_intp dims[2] = {n, (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  if (common->combo_push_count_threshold == 0u || common->combo_push_timer_frames == 0u) {
    return (PyObject*)out;
  }

  const uint8_t* counts_p = (const uint8_t*)PyArray_DATA(counts);
  const uint8_t* attacks_p = (const uint8_t*)PyArray_DATA(attacks);
  const uint8_t* victims_p = victims != NULL ? (const uint8_t*)PyArray_DATA(victims) : NULL;
  uint16_t timer[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_count[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_attack[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_victim[MSL_MAX_PLAYERS];
  uint8_t repeated[MSL_MAX_PLAYERS] = {0};
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    prev_victim[i] = 0xFFu;
  }
  const int players = (count_w < (npy_intp)MSL_MAX_PLAYERS) ? (int)count_w : MSL_MAX_PLAYERS;
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < players; p++) {
      const npy_intp pi = fi * count_w + p;
      const uint8_t cur = counts_p[pi];
      const uint8_t attack = attacks_p[pi];
      const uint8_t victim = victims_p != NULL ? victims_p[pi] : 0xFFu;
      const uint8_t same_victim =
          victims_p == NULL || (victim != 0xFFu && victim == prev_victim[p]) ? 1u : 0u;
      const uint8_t same_attack = (attack != 0u && attack == prev_attack[p]) ? 1u : 0u;
      const uint8_t increment = (cur > prev_count[p]) ? 1u : 0u;
      if (cur == 0u || attack == 0u) {
        repeated[p] = 0u;
      } else if (increment) {
        if (same_attack && same_victim) {
          repeated[p] = repeated[p] == 0xFFu ? 0xFFu : (uint8_t)(repeated[p] + 1u);
        } else {
          repeated[p] = 1u;
        }
      } else if (!(same_attack && same_victim)) {
        repeated[p] = 1u;
      }

      if (increment && cur >= common->combo_push_count_threshold &&
          repeated[p] >= common->combo_push_count_threshold) {
        timer[p] = common->combo_push_timer_frames;
      } else if (timer[p] != 0u) {
        timer[p] = (uint16_t)(timer[p] - 1u);
      }
      prev_count[p] = cur;
      prev_attack[p] = attack;
      prev_victim[p] = victim;
    }
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      out_p[fi * (npy_intp)MSL_MAX_PLAYERS + p] = timer[p];
    }
  }
  return (PyObject*)out;
}

static inline uint8_t msl_py_u8_sat_inc_fe(uint8_t v) {
  return v < 0xFEu ? (uint8_t)(v + 1u) : 0xFEu;
}

static inline uint8_t msl_py_u8_sat_inc_ff(uint8_t v) {
  return v < 0xFFu ? (uint8_t)(v + 1u) : 0xFFu;
}

static inline uint8_t msl_py_lb_8000D148(float point0_x, float point0_y, float point1_x,
                                         float point1_y, float point2_x, float point2_y,
                                         float threshold) {
  const float diff_01_y = point0_y - point1_y;
  const float diff_01_x = point1_x - point0_x;
  const float dist_squared_01 = diff_01_x * diff_01_x + diff_01_y * diff_01_y;
  if (dist_squared_01 < 0.00001f) {
    return 0u;
  }
  const float dist_01 = sqrtf(dist_squared_01);
  float var_f0 = ((point0_x * point1_y) - (point0_y * point1_x)) +
                 ((diff_01_x * point2_x) + (diff_01_y * point2_y));
  if (var_f0 < 0.0f) {
    var_f0 = -var_f0;
  }
  const float thr = threshold;
  if ((var_f0 / dist_01) <= thr) {
    const float diff_02_x = point0_x - point2_x;
    const float diff_02_y = point0_y - point2_y;
    const float diff_12_x = point1_x - point2_x;
    const float diff_12_y = point1_y - point2_y;
    const float threshold_squared = thr * thr;
    const float dist_squared_02 = diff_02_x * diff_02_x + diff_02_y * diff_02_y;
    const float dist_squared_12 = diff_12_x * diff_12_x + diff_12_y * diff_12_y;
    if (dist_squared_02 < threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        return 1u;
      }
      if (dist_squared_12 < threshold_squared) {
        return 0u;
      }
      return 1u;
    }
    if (dist_squared_02 > threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        if (((point0_x > point2_x) && (point1_x < point2_x)) ||
            ((point0_x < point2_x) && (point1_x > point2_x)) ||
            ((point0_y > point2_y) && (point1_y < point2_y)) ||
            ((point0_y < point2_y) && (point1_y > point2_y))) {
          return 1u;
        }
        return 0u;
      }
      if (dist_squared_12 < threshold_squared) {
        return 1u;
      }
      return 1u;
    }
    return 1u;
  }
  return 0u;
}

static PyObject* msl_compute_fighter_stick_input_counters_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* sx_obj = NULL;
  PyObject* sy_obj = NULL;
  double tilt_thresh_x = 0.0;
  double tilt_thresh_y = 0.0;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "OOddi", &sx_obj, &sy_obj, &tilt_thresh_x, &tilt_thresh_y,
                        &start_timer)) {
    return NULL;
  }
  PyArrayObject* sx_arr = require_contiguous_array(sx_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* sy_arr = require_contiguous_array(sy_obj, NPY_FLOAT32, 1, "stick_y_unit");
  if (sx_arr == NULL || sy_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(sx_arr);
  if (PyArray_SIZE(sy_arr) != n) {
    PyErr_SetString(PyExc_ValueError, "stick_y_unit must match stick_x_unit length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x673 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x674 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x676_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x2228_b7 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x677_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x679_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67A_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x673 == NULL || out_x674 == NULL || out_x676_x == NULL || out_x2228_b7 == NULL ||
      out_x677_y == NULL || out_x679_x == NULL || out_x67A_y == NULL) {
    Py_XDECREF(out_x673);
    Py_XDECREF(out_x674);
    Py_XDECREF(out_x676_x);
    Py_XDECREF(out_x2228_b7);
    Py_XDECREF(out_x677_y);
    Py_XDECREF(out_x679_x);
    Py_XDECREF(out_x67A_y);
    return NULL;
  }
  const float* sx = (const float*)PyArray_DATA(sx_arr);
  const float* sy = (const float*)PyArray_DATA(sy_arr);
  uint8_t* ox673 = (uint8_t*)PyArray_DATA(out_x673);
  uint8_t* ox674 = (uint8_t*)PyArray_DATA(out_x674);
  uint8_t* ox676 = (uint8_t*)PyArray_DATA(out_x676_x);
  uint8_t* ox2228 = (uint8_t*)PyArray_DATA(out_x2228_b7);
  uint8_t* ox677 = (uint8_t*)PyArray_DATA(out_x677_y);
  uint8_t* ox679 = (uint8_t*)PyArray_DATA(out_x679_x);
  uint8_t* ox67A = (uint8_t*)PyArray_DATA(out_x67A_y);
  const float thr_x = (float)tilt_thresh_x;
  const float thr_y = (float)tilt_thresh_y;
  uint8_t x673 = (uint8_t)start_timer;
  uint8_t x676 = (uint8_t)start_timer;
  uint8_t x2228 = 0u;
  uint8_t x679 = (uint8_t)start_timer;
  uint8_t x674 = (uint8_t)start_timer;
  uint8_t x677 = (uint8_t)start_timer;
  uint8_t x67A = (uint8_t)start_timer;
  float prev_x = 0.0f;
  float prev_y = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const float cur_x = sx[i];
    const float cur_y = sy[i];
    x676 = msl_py_u8_sat_inc_fe(x676);
    if (cur_x >= thr_x) {
      if (prev_x >= thr_x) {
        x673 = msl_py_u8_sat_inc_fe(x673);
        x679 = msl_py_u8_sat_inc_fe(x679);
      } else {
        x676 = 0u;
        x673 = 0u;
        x2228 = 1u;
      }
    } else if (cur_x <= -thr_x) {
      if (prev_x <= -thr_x) {
        x673 = msl_py_u8_sat_inc_fe(x673);
        x679 = msl_py_u8_sat_inc_fe(x679);
      } else {
        x676 = 0u;
        x673 = 0u;
        x2228 = 0u;
      }
    } else {
      x679 = 0xFEu;
      x673 = 0xFEu;
    }
    x677 = msl_py_u8_sat_inc_fe(x677);
    if (cur_y >= thr_y) {
      if (prev_y >= thr_y) {
        x674 = msl_py_u8_sat_inc_fe(x674);
        x67A = msl_py_u8_sat_inc_fe(x67A);
      } else {
        x677 = 0u;
        x674 = 0u;
      }
    } else if (cur_y <= -thr_y) {
      if (prev_y <= -thr_y) {
        x674 = msl_py_u8_sat_inc_fe(x674);
        x67A = msl_py_u8_sat_inc_fe(x67A);
      } else {
        x677 = 0u;
        x674 = 0u;
      }
    } else {
      x67A = 0xFEu;
      x674 = 0xFEu;
    }
    if (msl_py_lb_8000D148(prev_x, prev_y, cur_x, cur_y, 0.0f, 0.0f, thr_x)) {
      x67A = 0u;
      x679 = 0u;
    }
    ox673[i] = x673;
    ox674[i] = x674;
    ox676[i] = x676;
    ox2228[i] = x2228;
    ox677[i] = x677;
    ox679[i] = x679;
    ox67A[i] = x67A;
    prev_x = cur_x;
    prev_y = cur_y;
  }
  return Py_BuildValue("NNNNNNN", out_x673, out_x674, out_x676_x, out_x2228_b7, out_x677_y,
                       out_x679_x, out_x67A_y);
}

static PyObject* msl_compute_fighter_trigger_input_counters_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* trig_obj = NULL;
  double trigger_min = 0.0;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "Odi", &trig_obj, &trigger_min, &start_timer)) {
    return NULL;
  }
  PyArrayObject* trig_arr = require_contiguous_array(trig_obj, NPY_FLOAT32, 1, "trigger_unit");
  if (trig_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(trig_arr);
  npy_intp dims[1] = {n};
  PyArrayObject* out_x675 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67B = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x678 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x675 == NULL || out_x67B == NULL || out_x678 == NULL) {
    Py_XDECREF(out_x675);
    Py_XDECREF(out_x67B);
    Py_XDECREF(out_x678);
    return NULL;
  }
  const float* trig = (const float*)PyArray_DATA(trig_arr);
  uint8_t* ox675 = (uint8_t*)PyArray_DATA(out_x675);
  uint8_t* ox67B = (uint8_t*)PyArray_DATA(out_x67B);
  uint8_t* ox678 = (uint8_t*)PyArray_DATA(out_x678);
  const float thr = (float)trigger_min;
  uint8_t x675 = (uint8_t)start_timer;
  uint8_t x67B = (uint8_t)start_timer;
  uint8_t x678 = (uint8_t)start_timer;
  float prev = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const float cur = trig[i];
    x678 = msl_py_u8_sat_inc_fe(x678);
    if (cur >= thr) {
      if (prev >= thr) {
        x675 = msl_py_u8_sat_inc_fe(x675);
        x67B = msl_py_u8_sat_inc_fe(x67B);
      } else {
        x67B = 0u;
        x678 = 0u;
        x675 = 0u;
      }
    } else {
      x67B = 0xFEu;
      x675 = 0xFEu;
    }
    ox675[i] = x675;
    ox67B[i] = x67B;
    ox678[i] = x678;
    prev = cur;
  }
  return Py_BuildValue("NNN", out_x675, out_x67B, out_x678);
}

static PyObject* msl_compute_fighter_button_timers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* buttons_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  int mask_a = 0;
  int mask_b = 0;
  int mask_xy = 0;
  int mask_dpad_up = 0;
  int mask_dpad_down = 0;
  int mask_lr = 0;
  int mask_z = 0;
  int start_timer = 0xFF;
  if (!PyArg_ParseTuple(args, "OOiiiiiiii", &buttons_obj, &hitlag_obj, &mask_a, &mask_b, &mask_xy,
                        &mask_dpad_up, &mask_dpad_down, &mask_lr, &mask_z, &start_timer)) {
    return NULL;
  }
  PyArrayObject* buttons_arr =
      require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_pressed");
  if (buttons_arr == NULL) {
    return NULL;
  }
  PyArrayObject* hitlag_arr = NULL;
  if (hitlag_obj != Py_None) {
    hitlag_arr = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_frames");
    if (hitlag_arr == NULL) {
      return NULL;
    }
  }
  const npy_intp n = PyArray_SIZE(buttons_arr);
  if (hitlag_arr != NULL && PyArray_SIZE(hitlag_arr) != n) {
    PyErr_SetString(PyExc_ValueError, "hitlag_frames must match buttons_pressed length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x67C = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67D = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67E = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x680 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x681 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x682 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x683 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x684 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x67C == NULL || out_x67D == NULL || out_x67E == NULL || out_x680 == NULL ||
      out_x681 == NULL || out_x682 == NULL || out_x683 == NULL || out_x684 == NULL) {
    Py_XDECREF(out_x67C);
    Py_XDECREF(out_x67D);
    Py_XDECREF(out_x67E);
    Py_XDECREF(out_x680);
    Py_XDECREF(out_x681);
    Py_XDECREF(out_x682);
    Py_XDECREF(out_x683);
    Py_XDECREF(out_x684);
    return NULL;
  }
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(buttons_arr);
  const uint16_t* hl = hitlag_arr != NULL ? (const uint16_t*)PyArray_DATA(hitlag_arr) : NULL;
  uint8_t* ox67C = (uint8_t*)PyArray_DATA(out_x67C);
  uint8_t* ox67D = (uint8_t*)PyArray_DATA(out_x67D);
  uint8_t* ox67E = (uint8_t*)PyArray_DATA(out_x67E);
  uint8_t* ox680 = (uint8_t*)PyArray_DATA(out_x680);
  uint8_t* ox681 = (uint8_t*)PyArray_DATA(out_x681);
  uint8_t* ox682 = (uint8_t*)PyArray_DATA(out_x682);
  uint8_t* ox683 = (uint8_t*)PyArray_DATA(out_x683);
  uint8_t* ox684 = (uint8_t*)PyArray_DATA(out_x684);
  uint8_t x67C = (uint8_t)start_timer;
  uint8_t x67D = (uint8_t)start_timer;
  uint8_t x67E = (uint8_t)start_timer;
  uint8_t x680 = (uint8_t)start_timer;
  uint8_t x681 = (uint8_t)start_timer;
  uint8_t x682 = (uint8_t)start_timer;
  uint8_t x683 = (uint8_t)start_timer;
  uint8_t x684 = (uint8_t)start_timer;
  uint16_t x668_latched = 0u;
  const uint16_t m_a = (uint16_t)mask_a;
  const uint16_t m_b = (uint16_t)mask_b;
  const uint16_t m_xy = (uint16_t)mask_xy;
  const uint16_t m_du = (uint16_t)mask_dpad_up;
  const uint16_t m_dd = (uint16_t)mask_dpad_down;
  const uint16_t m_lr = (uint16_t)mask_lr;
  const uint16_t m_z = (uint16_t)mask_z;
  for (npy_intp i = 0; i < n; i++) {
    uint16_t raw = bp[i];
    if ((raw & m_z) != 0u) {
      raw = (uint16_t)(raw | m_a);
    }
    uint16_t bpi = raw;
    if (hl != NULL && hl[i] > 0u) {
      x668_latched = (uint16_t)(x668_latched | raw);
      bpi = x668_latched;
    } else {
      x668_latched = 0u;
    }
    if ((bpi & m_a) != 0u) {
      x683 = x67C;
      x67C = 0u;
    } else {
      x67C = msl_py_u8_sat_inc_ff(x67C);
    }
    if ((bpi & m_b) != 0u) {
      x67D = 0u;
    } else {
      x67D = msl_py_u8_sat_inc_ff(x67D);
    }
    if ((bpi & m_xy) != 0u) {
      x67E = 0u;
    } else {
      x67E = msl_py_u8_sat_inc_ff(x67E);
    }
    if ((bpi & m_du) != 0u) {
      x681 = 0u;
    } else {
      x681 = msl_py_u8_sat_inc_ff(x681);
    }
    if ((bpi & m_dd) != 0u) {
      x682 = 0u;
    } else {
      x682 = msl_py_u8_sat_inc_ff(x682);
    }
    if ((bpi & m_lr) != 0u) {
      x684 = x680;
      x680 = 0u;
    } else {
      x680 = msl_py_u8_sat_inc_ff(x680);
    }
    ox67C[i] = x67C;
    ox67D[i] = x67D;
    ox67E[i] = x67E;
    ox680[i] = x680;
    ox681[i] = x681;
    ox682[i] = x682;
    ox683[i] = x683;
    ox684[i] = x684;
  }
  return Py_BuildValue("NNNNNNNN", out_x67C, out_x67D, out_x67E, out_x680, out_x681, out_x682,
                       out_x683, out_x684);
}

static PyObject* msl_derive_illusion_ghost_pos012_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOO", &action_obj, &action_frame_obj, &pos_x_obj, &pos_y_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "post_action_id_u16");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 2, "post_action_frame_i16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "post_pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "post_pos_y");
  if (action == NULL || action_frame == NULL || pos_x == NULL || pos_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp players = PyArray_DIM(action, 1);
  if (PyArray_DIM(action_frame, 0) != n || PyArray_DIM(action_frame, 1) != players ||
      PyArray_DIM(pos_x, 0) != n || PyArray_DIM(pos_x, 1) != players ||
      PyArray_DIM(pos_y, 0) != n || PyArray_DIM(pos_y, 1) != players) {
    PyErr_SetString(PyExc_ValueError, "illusion ghost inputs must share [frames, players]");
    return NULL;
  }
  npy_intp dims[2] = {n, players};
  PyArrayObject* out0_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out0_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out1_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out1_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out2_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out2_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  if (out0_x == NULL || out0_y == NULL || out1_x == NULL || out1_y == NULL || out2_x == NULL ||
      out2_y == NULL) {
    Py_XDECREF(out0_x);
    Py_XDECREF(out0_y);
    Py_XDECREF(out1_x);
    Py_XDECREF(out1_y);
    Py_XDECREF(out2_x);
    Py_XDECREF(out2_y);
    return NULL;
  }
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action);
  const int16_t* frame_p = (const int16_t*)PyArray_DATA(action_frame);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  float* o0x = (float*)PyArray_DATA(out0_x);
  float* o0y = (float*)PyArray_DATA(out0_y);
  float* o1x = (float*)PyArray_DATA(out1_x);
  float* o1y = (float*)PyArray_DATA(out1_y);
  float* o2x = (float*)PyArray_DATA(out2_x);
  float* o2y = (float*)PyArray_DATA(out2_y);
  for (npy_intp p = 0; p < players; p++) {
    float ghost0_x = n > 0 ? px[p] : 0.0f;
    float ghost0_y = n > 0 ? py[p] : 0.0f;
    float ghost1_x = ghost0_x;
    float ghost1_y = ghost0_y;
    float ghost2_x = ghost0_x;
    float ghost2_y = ghost0_y;
    for (npy_intp fi = 0; fi < n; fi++) {
      const npy_intp idx = fi * players + p;
      const uint16_t cur_a = action_p[idx];
      const float cur_x = px[idx];
      const float cur_y = py[idx];
      uint8_t entry_main = 0u;
      if (cur_a == 348u || cur_a == 351u) {
        if (fi == 0) {
          entry_main = 1u;
        } else {
          const npy_intp prev = (fi - 1) * players + p;
          if (action_p[prev] != cur_a || frame_p[idx] < frame_p[prev]) {
            entry_main = 1u;
          }
        }
      }
      if (entry_main) {
        ghost0_x = cur_x;
        ghost0_y = cur_y;
        ghost1_x = cur_x;
        ghost1_y = cur_y;
        ghost2_x = cur_x;
        ghost2_y = cur_y;
      } else if (cur_a == 348u || cur_a == 349u || cur_a == 351u || cur_a == 352u) {
        ghost2_x = ghost1_x;
        ghost2_y = ghost1_y;
        ghost1_x = ghost0_x;
        ghost1_y = ghost0_y;
        ghost0_x = cur_x;
        ghost0_y = cur_y;
      }
      o0x[idx] = ghost0_x;
      o0y[idx] = ghost0_y;
      o1x[idx] = ghost1_x;
      o1y[idx] = ghost1_y;
      o2x[idx] = ghost2_x;
      o2y[idx] = ghost2_y;
    }
  }
  return Py_BuildValue("NNNNNN", out0_x, out0_y, out1_x, out1_y, out2_x, out2_y);
}

typedef struct MslPyHbPrim {
  uint8_t valid;
  uint16_t flags;
  int16_t def_frame;
  uint8_t group;
  uint8_t rehit;
  float x;
  float y;
  float z;
  float r;
  float damage;
} MslPyHbPrim;

typedef struct MslPyCapPrim {
  uint8_t valid;
  float ax;
  float ay;
  float az;
  float bx;
  float by;
  float bz;
  float r;
} MslPyCapPrim;

static inline uint8_t msl_py_is_shield_active_action(uint16_t action_id) {
  return (action_id == MSL_ACT_GUARD_ON || action_id == MSL_ACT_GUARD ||
          action_id == MSL_ACT_GUARD_REFLECT || action_id == MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

static inline uint8_t msl_py_is_attackair_action(uint16_t action_id) {
  return (action_id >= MSL_ACT_ATTACK_AIR_N && action_id <= MSL_ACT_ATTACK_AIR_LW) ? 1u : 0u;
}

static inline uint8_t msl_py_hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id) {
  if (stocks == 0u) {
    return 1u;
  }
  return (action_id == MSL_ACT_DEAD_DOWN || action_id == MSL_ACT_DEAD_LEFT ||
          action_id == MSL_ACT_DEAD_RIGHT || action_id == MSL_ACT_DEAD_UP_STAR ||
          action_id == MSL_ACT_REBIRTH || action_id == MSL_ACT_REBIRTH_WAIT)
             ? 1u
             : 0u;
}

static inline uint8_t msl_py_sphere_sphere_intersects(float ax, float ay, float az, float ar,
                                                      float bx, float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr) ? 1u : 0u;
}

static inline float msl_py_point_segment_dist2(float px, float py, float pz, float ax, float ay,
                                               float az, float bx, float by, float bz) {
  const float abx = bx - ax;
  const float aby = by - ay;
  const float abz = bz - az;
  const float apx = px - ax;
  const float apy = py - ay;
  const float apz = pz - az;
  const float denom = abx * abx + aby * aby + abz * abz;
  float t = 0.0f;
  if (denom > 0.0f) {
    t = (apx * abx + apy * aby + apz * abz) / denom;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float qx = ax + t * abx;
  const float qy = ay + t * aby;
  const float qz = az + t * abz;
  const float dx = px - qx;
  const float dy = py - qy;
  const float dz = pz - qz;
  return dx * dx + dy * dy + dz * dz;
}

static inline uint8_t msl_py_sphere_capsule_intersects(float sx, float sy, float sz, float r_sphere,
                                                       float ax, float ay, float az, float bx,
                                                       float by, float bz, float r_capsule) {
  const float d2 = msl_py_point_segment_dist2(sx, sy, sz, ax, ay, az, bx, by, bz);
  const float r = r_sphere + r_capsule;
  return d2 <= (r * r) ? 1u : 0u;
}

static inline float msl_py_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float msl_py_trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  if ((buttons & (uint16_t)(0x0040u | 0x0020u)) != 0u) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return (float)m * (1.0f / 255.0f);
}

static inline int msl_py_get_env_dmg(float dmg) {
  if (dmg == 0.0f) {
    return 0;
  }
  const int i = (int)dmg;
  return i != 0 ? i : 1;
}

static inline uint16_t msl_py_calc_hitlag_frames(const MslCommonParams* common, int dmg_int) {
  float tmp_f = (float)dmg_int * common->hitlag_dmg_mul + common->hitlag_base;
  int tmp = (int)tmp_f;
  if (tmp < 0) {
    tmp = 0;
  }
  if (tmp > 0xFFFF) {
    tmp = 0xFFFF;
  }
  return (uint16_t)tmp;
}

static PyObject* msl_derive_combat_hitlist_seed_fields_py(PyObject* self, PyObject* args) {
  (void)self;
  int num_players = 0;
  int is_teams = 0;
  int include_per_hitbox = 0;
  int include_replay_only_shield_admission = 0;
  int include_replay_only_body_admission = 0;
  PyObject* team_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* guard_x8_obj = NULL;
  PyObject* guard_x4_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* shield_hp_obj = NULL;
  PyObject* hurtbox_state_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  PyObject* last_hit_by_obj = Py_None;
  PyObject* instance_hit_by_obj = Py_None;
  PyObject* instance_id_obj = NULL;
  PyObject* input_buttons_obj = NULL;
  PyObject* input_l_obj = NULL;
  PyObject* input_r_obj = NULL;
  PyObject* turn_has_turned_obj = Py_None;
  PyObject* anim_frame_obj = Py_None;
  PyObject* frame_speed_obj = Py_None;
  PyObject* rotate_model_obj = Py_None;
  PyObject* rotate_valid_obj = Py_None;
  PyObject* percent_obj = Py_None;
  if (!PyArg_ParseTuple(
          args, "iiOOOOOOOOOOOOOOOOOOOOOOOOOOOOiii", &num_players, &is_teams, &team_obj, &char_obj,
          &action_obj, &action_frame_obj, &anim_obj, &facing_obj, &on_ground_obj, &pos_x_obj,
          &pos_y_obj, &scale_y_obj, &guard_x8_obj, &guard_x4_obj, &stocks_obj, &shield_hp_obj,
          &hurtbox_state_obj, &hitlag_obj, &last_hit_by_obj, &instance_hit_by_obj, &instance_id_obj,
          &input_buttons_obj, &input_l_obj, &input_r_obj, &turn_has_turned_obj, &anim_frame_obj,
          &frame_speed_obj, &rotate_model_obj, &rotate_valid_obj, &percent_obj, &include_per_hitbox,
          &include_replay_only_shield_admission, &include_replay_only_body_admission)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }

#define REQ_ARR(name, obj, typenum, label)                                      \
  PyArrayObject* name = require_contiguous_array((obj), (typenum), 2, (label)); \
  if ((name) == NULL) {                                                         \
    return NULL;                                                                \
  }
  REQ_ARR(team, team_obj, NPY_UINT8, "team_id");
  REQ_ARR(char_id, char_obj, NPY_UINT8, "char_id");
  REQ_ARR(action_id, action_obj, NPY_UINT16, "action_id");
  REQ_ARR(action_frame, action_frame_obj, NPY_INT16, "action_frame");
  REQ_ARR(anim, anim_obj, NPY_UINT32, "animation_index");
  REQ_ARR(facing, facing_obj, NPY_UINT8, "facing");
  REQ_ARR(on_ground, on_ground_obj, NPY_UINT8, "on_ground");
  REQ_ARR(pos_x, pos_x_obj, NPY_FLOAT32, "pos_x");
  REQ_ARR(pos_y, pos_y_obj, NPY_FLOAT32, "pos_y");
  REQ_ARR(scale_y, scale_y_obj, NPY_FLOAT32, "fighter_scale_y");
  REQ_ARR(guard_x8, guard_x8_obj, NPY_UINT16, "guard_tilt_x8");
  REQ_ARR(guard_x4, guard_x4_obj, NPY_FLOAT32, "guard_tilt_x4");
  REQ_ARR(stocks, stocks_obj, NPY_UINT8, "stocks");
  REQ_ARR(shield_hp, shield_hp_obj, NPY_FLOAT32, "shield_hp");
  REQ_ARR(hurtbox_state, hurtbox_state_obj, NPY_UINT8, "hurtbox_state");
  REQ_ARR(instance_id, instance_id_obj, NPY_UINT16, "instance_id");
  REQ_ARR(input_buttons, input_buttons_obj, NPY_UINT16, "input_buttons");
  REQ_ARR(input_l, input_l_obj, NPY_UINT8, "input_l");
  REQ_ARR(input_r, input_r_obj, NPY_UINT8, "input_r");
#undef REQ_ARR

#define OPT_ARR(name, obj, typenum, label)                         \
  PyArrayObject* name = NULL;                                      \
  if ((obj) != Py_None) {                                          \
    name = require_contiguous_array((obj), (typenum), 2, (label)); \
    if ((name) == NULL) {                                          \
      return NULL;                                                 \
    }                                                              \
  }
  OPT_ARR(hitlag, hitlag_obj, NPY_UINT16, "hitlag");
  OPT_ARR(last_hit_by, last_hit_by_obj, NPY_UINT8, "last_hit_by");
  OPT_ARR(instance_hit_by, instance_hit_by_obj, NPY_UINT16, "instance_hit_by");
  OPT_ARR(turn_has_turned, turn_has_turned_obj, NPY_UINT8, "turn_has_turned");
  OPT_ARR(anim_frame, anim_frame_obj, NPY_FLOAT32, "anim_frame_f32");
  OPT_ARR(frame_speed, frame_speed_obj, NPY_FLOAT32, "frame_speed_mul_f32");
  OPT_ARR(rotate_model, rotate_model_obj, NPY_FLOAT32, "specialhi_rotate_model_f32");
  OPT_ARR(rotate_valid, rotate_valid_obj, NPY_UINT8, "specialhi_rotate_model_valid_u8");
  OPT_ARR(percent, percent_obj, NPY_FLOAT32, "percent");
#undef OPT_ARR

  const npy_intp n = PyArray_DIM(action_id, 0);
  const npy_intp width = PyArray_DIM(action_id, 1);
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "action_id width smaller than num_players");
    return NULL;
  }
#define CHECK_DIMS(arr, label)                                 \
  if (require_exact_2d_shape((arr), n, width, (label)) != 0) { \
    return NULL;                                               \
  }
  CHECK_DIMS(team, "team_id");
  CHECK_DIMS(char_id, "char_id");
  CHECK_DIMS(action_frame, "action_frame");
  CHECK_DIMS(anim, "animation_index");
  CHECK_DIMS(facing, "facing");
  CHECK_DIMS(on_ground, "on_ground");
  CHECK_DIMS(pos_x, "pos_x");
  CHECK_DIMS(pos_y, "pos_y");
  CHECK_DIMS(scale_y, "fighter_scale_y");
  CHECK_DIMS(guard_x8, "guard_tilt_x8");
  CHECK_DIMS(guard_x4, "guard_tilt_x4");
  CHECK_DIMS(stocks, "stocks");
  CHECK_DIMS(shield_hp, "shield_hp");
  CHECK_DIMS(hurtbox_state, "hurtbox_state");
  CHECK_DIMS(instance_id, "instance_id");
  CHECK_DIMS(input_buttons, "input_buttons");
  CHECK_DIMS(input_l, "input_l");
  CHECK_DIMS(input_r, "input_r");
  if (hitlag != NULL) CHECK_DIMS(hitlag, "hitlag");
  if (last_hit_by != NULL) CHECK_DIMS(last_hit_by, "last_hit_by");
  if (instance_hit_by != NULL) CHECK_DIMS(instance_hit_by, "instance_hit_by");
  if (turn_has_turned != NULL) CHECK_DIMS(turn_has_turned, "turn_has_turned");
  if (anim_frame != NULL) CHECK_DIMS(anim_frame, "anim_frame_f32");
  if (frame_speed != NULL) CHECK_DIMS(frame_speed, "frame_speed_mul_f32");
  if (rotate_model != NULL) CHECK_DIMS(rotate_model, "specialhi_rotate_model_f32");
  if (rotate_valid != NULL) CHECK_DIMS(rotate_valid, "specialhi_rotate_model_valid_u8");
  if (percent != NULL) CHECK_DIMS(percent, "percent");
#undef CHECK_DIMS

  if (common_params_init() != 0 || char_params_init() != 0 || anim_pose_init() != 0 ||
      anim_table_init() != 0 || hitboxes_tables_init() != 0 || hurtcaps_tables_init() != 0 ||
      shield_tilt_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "combat hitlist native tables failed to initialize");
    return NULL;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "common params unavailable");
    return NULL;
  }

  npy_intp dims_dense[4] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_HITLIST_GROUPS,
                            (npy_intp)MSL_MAX_PLAYERS};
  npy_intp dims_hb_valid[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  npy_intp dims_hb[4] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES,
                         (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out_cd = (PyArrayObject*)PyArray_ZEROS(4, dims_dense, NPY_UINT16, 0);
  PyArrayObject* out_iid = (PyArrayObject*)PyArray_ZEROS(4, dims_dense, NPY_UINT16, 0);
  PyArrayObject* out_hb_valid = (PyArrayObject*)PyArray_ZEROS(3, dims_hb_valid, NPY_UINT8, 0);
  PyArrayObject* out_hb_cd = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT16, 0);
  PyArrayObject* out_hb_iid = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT16, 0);
  PyArrayObject* out_shield_kind = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT8, 0);
  if (out_cd == NULL || out_iid == NULL || out_hb_valid == NULL || out_hb_cd == NULL ||
      out_hb_iid == NULL || out_shield_kind == NULL) {
    Py_XDECREF(out_cd);
    Py_XDECREF(out_iid);
    Py_XDECREF(out_hb_valid);
    Py_XDECREF(out_hb_cd);
    Py_XDECREF(out_hb_iid);
    Py_XDECREF(out_shield_kind);
    return NULL;
  }

#define PTR(name, type, arr) const type* name = (const type*)PyArray_DATA(arr)
  PTR(team_p, uint8_t, team);
  PTR(char_p, uint8_t, char_id);
  PTR(action_p, uint16_t, action_id);
  PTR(action_frame_p, int16_t, action_frame);
  PTR(anim_p, uint32_t, anim);
  PTR(facing_p, uint8_t, facing);
  PTR(on_ground_p, uint8_t, on_ground);
  PTR(pos_x_p, float, pos_x);
  PTR(pos_y_p, float, pos_y);
  PTR(scale_y_p, float, scale_y);
  PTR(guard_x8_p, uint16_t, guard_x8);
  PTR(guard_x4_p, float, guard_x4);
  PTR(stocks_p, uint8_t, stocks);
  PTR(shield_hp_p, float, shield_hp);
  PTR(hurtbox_state_p, uint8_t, hurtbox_state);
  PTR(instance_id_p, uint16_t, instance_id);
  PTR(input_buttons_p, uint16_t, input_buttons);
  PTR(input_l_p, uint8_t, input_l);
  PTR(input_r_p, uint8_t, input_r);
#undef PTR
  const uint16_t* hitlag_p = hitlag != NULL ? (const uint16_t*)PyArray_DATA(hitlag) : NULL;
  const uint8_t* last_hit_by_p =
      last_hit_by != NULL ? (const uint8_t*)PyArray_DATA(last_hit_by) : NULL;
  const uint16_t* instance_hit_by_p =
      instance_hit_by != NULL ? (const uint16_t*)PyArray_DATA(instance_hit_by) : NULL;
  const uint8_t* turn_has_turned_p =
      turn_has_turned != NULL ? (const uint8_t*)PyArray_DATA(turn_has_turned) : NULL;
  const float* anim_frame_p = anim_frame != NULL ? (const float*)PyArray_DATA(anim_frame) : NULL;
  const float* frame_speed_p = frame_speed != NULL ? (const float*)PyArray_DATA(frame_speed) : NULL;
  const float* rotate_model_p =
      rotate_model != NULL ? (const float*)PyArray_DATA(rotate_model) : NULL;
  const uint8_t* rotate_valid_p =
      rotate_valid != NULL ? (const uint8_t*)PyArray_DATA(rotate_valid) : NULL;
  const float* percent_p = percent != NULL ? (const float*)PyArray_DATA(percent) : NULL;

  uint16_t* out_cd_p = (uint16_t*)PyArray_DATA(out_cd);
  uint16_t* out_iid_p = (uint16_t*)PyArray_DATA(out_iid);
  uint8_t* out_hb_valid_p = (uint8_t*)PyArray_DATA(out_hb_valid);
  uint16_t* out_hb_cd_p = (uint16_t*)PyArray_DATA(out_hb_cd);
  uint16_t* out_hb_iid_p = (uint16_t*)PyArray_DATA(out_hb_iid);
  uint8_t* out_shield_kind_p = (uint8_t*)PyArray_DATA(out_shield_kind);

  uint16_t hitlist_cd[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_iid[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_hb_cd[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_hb_iid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS] = {{{0}}};
  uint8_t hitlist_hb_authoritative[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
  uint16_t sim_hitlag[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_group_active[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS] = {{0}};
  uint8_t prev_hb_active[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
  uint8_t prev_hb_group[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};

  const float denom = 1.0f - common->trigger_deadzone;

  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      if (sim_hitlag[p] != 0u) {
        sim_hitlag[p] = (uint16_t)(sim_hitlag[p] - 1u);
      }
    }

    MslPyHbPrim hitboxes[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
    MslPyCapPrim caps[MSL_MAX_PLAYERS][MSL_MAX_HURTCAPS];
    uint16_t cap_counts[MSL_MAX_PLAYERS] = {0};
    float shield_x[MSL_MAX_PLAYERS] = {0.0f};
    float shield_y[MSL_MAX_PLAYERS] = {0.0f};
    float shield_z[MSL_MAX_PLAYERS] = {0.0f};
    float shield_r[MSL_MAX_PLAYERS] = {0.0f};
    uint8_t replay_only_hb_valid_frame[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
    memset(hitboxes, 0, sizeof(hitboxes));
    memset(caps, 0, sizeof(caps));

    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      const uint8_t cid = char_p[pi];
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL || action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      uint16_t frame = (uint16_t)action_frame_p[pi];
      if (anim_frame_p != NULL) {
        float af_f = anim_frame_p[pi];
        if (isfinite(af_f) && af_f >= 0.0f) {
          if (frame_speed_p != NULL && (hitlag_p == NULL || hitlag_p[pi] == 0u)) {
            af_f += frame_speed_p[pi];
          }
          if (af_f < 0.0f) {
            af_f = 0.0f;
          }
          if (af_f > 65535.0f) {
            af_f = 65535.0f;
          }
          frame = (uint16_t)floorf(af_f);
        }
      }

      const uint16_t msid = (uint16_t)anim_p[pi];
      const float model_scaling =
          (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
      const float model_scale = scale_y_p[pi] * model_scaling;
      const float scale_y_val = scale_y_p[pi];
      const float px = pos_x_p[pi];
      const float py = pos_y_p[pi];
      float facing_dir = facing_p[pi] ? 1.0f : -1.0f;
      if (action_p[pi] == MSL_ACT_TURN && turn_has_turned_p != NULL &&
          turn_has_turned_p[pi] != 0u) {
        facing_dir = -facing_dir;
      }
      const float rotate_model_val = rotate_model_p != NULL ? rotate_model_p[pi] : 0.0f;
      const uint8_t rotate_valid_val = rotate_valid_p != NULL ? rotate_valid_p[pi] : 0u;

      const MslHurtCap* hc = NULL;
      uint16_t hc_count = 0;
      if (hurtcaps_get(cid, &hc, &hc_count) == 0 && hc != NULL) {
        if (hc_count > MSL_MAX_HURTCAPS) {
          hc_count = MSL_MAX_HURTCAPS;
        }
        for (uint16_t ci = 0; ci < hc_count; ci++) {
          float m[12];
          if (anim_pose_get_matrix(cid, msid, frame, hc[ci].bone_part_id, m) != 0) {
            continue;
          }
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          msl_py_mtx34_mul_point(m, hc[ci].a_offset[0], hc[ci].a_offset[1], hc[ci].a_offset[2], &ax,
                                 &ay, &az);
          msl_py_mtx34_mul_point(m, hc[ci].b_offset[0], hc[ci].b_offset[1], hc[ci].b_offset[2], &bx,
                                 &by, &bz);
          ax *= model_scale;
          ay *= model_scale;
          az *= model_scale;
          bx *= model_scale;
          by *= model_scale;
          bz *= model_scale;
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, hc[ci].bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &ax,
                                             &ay, &az);
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, hc[ci].bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &bx,
                                             &by, &bz);
          MslPyCapPrim* out = &caps[p][cap_counts[p]++];
          out->valid = 1u;
          out->ax = facing_dir * az + px;
          out->ay = ay + py;
          out->az = -facing_dir * ax;
          out->bx = facing_dir * bz + px;
          out->by = by + py;
          out->bz = -facing_dir * bx;
          out->r = hc[ci].scale * model_scale;
        }
      }

      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(cid, msid, &events, &event_count) == 0 && events != NULL) {
        const MslHitboxEvent* active[MSL_MAX_HITBOXES] = {0};
        for (uint16_t ei = 0; ei < event_count; ei++) {
          const MslHitboxEvent* ev = &events[ei];
          if (ev->frame > frame) {
            continue;
          }
          if (ev->kind == 1u) {
            if (ev->hitbox_id == 0xFFu) {
              memset(active, 0, sizeof(active));
            } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
              active[ev->hitbox_id] = NULL;
            }
          } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
            active[ev->hitbox_id] = ev;
          }
        }
        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const MslHitboxEvent* ev = active[hb_id];
          if (ev == NULL) {
            continue;
          }
          float m[12];
          if (anim_pose_get_matrix(cid, msid, frame, ev->bone_part_id, m) != 0) {
            continue;
          }
          float lx = 0.0f, ly = 0.0f, lz = 0.0f;
          msl_py_mtx34_mul_point(m, ev->x, ev->y, ev->z, &lx, &ly, &lz);
          lx *= model_scale;
          ly *= model_scale;
          lz *= model_scale;
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, ev->bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &lx,
                                             &ly, &lz);
          MslPyHbPrim* hb = &hitboxes[p][hb_id];
          hb->valid = 1u;
          hb->x = facing_dir * lz + px;
          hb->y = ly + py;
          hb->z = -facing_dir * lx;
          hb->r = ev->radius;
          if ((ev->u16_6 & (uint16_t)MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE) == 0u) {
            hb->r *= scale_y_val;
          }
          hb->damage = ev->damage;
          hb->flags = ev->u16_6;
          hb->def_frame = (int16_t)ev->frame;
          hb->group = (uint8_t)((ev->u16_7 >> 8) & 0x7u);
          hb->rehit = (uint8_t)(ev->u16_7 & 0xFFu);
        }
      }

      shield_x[p] = px;
      shield_y[p] = py;
      shield_z[p] = 0.0f;
      shield_r[p] = 0.0f;
      if (stocks_p[pi] != 0u && msl_py_is_shield_active_action(action_p[pi])) {
        const float hp = shield_hp_p[pi];
        if (hp > 0.0f && common->start_shield_health > 0.0f) {
          const float trig =
              msl_py_trigger_unit_from_input(input_buttons_p[pi], input_l_p[pi], input_r_p[pi]);
          const float light =
              denom > 0.0f ? msl_py_clamp01((trig - common->trigger_deadzone) / denom) : 0.0f;
          const float hp_ratio = msl_py_clamp01(hp / common->start_shield_health);
          const float light_scale = (light * (common->shield_size_lightshield_max -
                                              common->shield_size_lightshield_min)) +
                                    common->shield_size_lightshield_min;
          const float n1 = hp_ratio * light_scale;
          const float n2 = 1.0f - common->shield_size_min_scale;
          const float s = (n2 * n1) + common->shield_size_min_scale;
          shield_r[p] = s * ch->initial_shield_size * scale_y_val;
          MslShieldTiltTableView tv;
          if (msl_shield_tilt_table_view(cid, &tv) == 0 && tv.xyz != NULL && tv.frame_count != 0u) {
            uint16_t f = guard_x8_p[pi];
            if (f >= tv.frame_count) {
              f = (uint16_t)(tv.frame_count - 1u);
            }
            float mag = msl_py_clamp01(guard_x4_p[pi]);
            const uint8_t steady_guard_no_tilt =
                (action_p[pi] == MSL_ACT_GUARD && (mag == 0.0f || mag < 1.1754943508222875e-38f))
                    ? 1u
                    : 0u;
            const uint16_t neutral = steady_guard_no_tilt ? 0u : tv.neutral_frame;
            const float* nxyz = tv.xyz + (size_t)neutral * 3u;
            const float* fxyz = tv.xyz + (size_t)f * 3u;
            const float dx = nxyz[0] + mag * (fxyz[0] - nxyz[0]);
            const float dy = nxyz[1] + mag * (fxyz[1] - nxyz[1]);
            const float dz = nxyz[2] + mag * (fxyz[2] - nxyz[2]);
            float pose_scale = scale_y_val;
            if (steady_guard_no_tilt) {
              pose_scale *= model_scaling;
            }
            const float fd = facing_p[pi] ? 1.0f : -1.0f;
            shield_x[p] = px + dz * pose_scale * fd;
            shield_y[p] = py + dy * pose_scale;
            shield_z[p] = -dx * pose_scale * fd;
          }
        }
      }
    }

    int pending_count = 0;
    struct {
      int attacker, group, defender, rehit, iid, hl;
    } pending[16];

    for (int attacker = 0; attacker < num_players; attacker++) {
      const npy_intp ai = fi * width + attacker;
      if (stocks_p[ai] == 0u) {
        memset(hitlist_hb_cd[attacker], 0, sizeof(hitlist_hb_cd[attacker]));
        memset(hitlist_hb_iid[attacker], 0, sizeof(hitlist_hb_iid[attacker]));
        memset(prev_hb_active[attacker], 0, sizeof(prev_hb_active[attacker]));
        memset(prev_group_active[attacker], 0, sizeof(prev_group_active[attacker]));
        continue;
      }

      uint8_t group_active[MSL_HITLIST_GROUPS] = {0};
      uint8_t any_hitboxes = 0u;
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (hitboxes[attacker][hb_id].valid) {
          any_hitboxes = 1u;
          group_active[hitboxes[attacker][hb_id].group & 0x7u] = 1u;
        }
      }
      const uint8_t clear_dense_on_enable_edge = action_p[ai] == MSL_ACT_ATTACK_HI3 ? 1u : 0u;
      if (clear_dense_on_enable_edge) {
        for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
          if (group_active[g] && !prev_group_active[attacker][g]) {
            for (int victim = 0; victim < num_players; victim++) {
              const npy_intp vi = fi * width + victim;
              if (action_p[vi] != MSL_ACT_DAMAGE_FLY_LW) {
                continue;
              }
              if (hitlag_p != NULL && hitlag_p[vi] != 0u) {
                continue;
              }
              hitlist_cd[attacker][g][victim] = 0u;
              hitlist_iid[attacker][g][victim] = 0u;
            }
          }
        }
      }
      memcpy(prev_group_active[attacker], group_active, sizeof(group_active));
      for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
        if (!group_active[g]) {
          continue;
        }
        for (int victim = 0; victim < num_players; victim++) {
          uint16_t cd = hitlist_cd[attacker][g][victim];
          if (cd == 0u || cd == 0xFFFFu) {
            continue;
          }
          cd = (uint16_t)(cd - 1u);
          hitlist_cd[attacker][g][victim] = cd;
          if (cd == 0u) {
            hitlist_iid[attacker][g][victim] = 0u;
          }
        }
      }

      uint16_t prev_cd[MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];
      uint16_t prev_iid[MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];
      memcpy(prev_cd, hitlist_hb_cd[attacker], sizeof(prev_cd));
      memcpy(prev_iid, hitlist_hb_iid[attacker], sizeof(prev_iid));
      uint8_t cur_active[MSL_MAX_HITBOXES] = {0};
      uint8_t cur_group[MSL_MAX_HITBOXES] = {0};
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (hitboxes[attacker][hb_id].valid) {
          cur_active[hb_id] = 1u;
          cur_group[hb_id] = hitboxes[attacker][hb_id].group & 0x7u;
        }
      }
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (!cur_active[hb_id]) {
          memset(hitlist_hb_cd[attacker][hb_id], 0, sizeof(hitlist_hb_cd[attacker][hb_id]));
          memset(hitlist_hb_iid[attacker][hb_id], 0, sizeof(hitlist_hb_iid[attacker][hb_id]));
          hitlist_hb_authoritative[attacker][hb_id] = 0u;
          continue;
        }
        const uint8_t g = cur_group[hb_id];
        const uint8_t enable_edge =
            (!prev_hb_active[attacker][hb_id] || prev_hb_group[attacker][hb_id] != g) ? 1u : 0u;
        if (enable_edge) {
          uint8_t copied = 0u;
          for (int src = 0; src < MSL_MAX_HITBOXES; src++) {
            if (src == hb_id || !prev_hb_active[attacker][src] ||
                prev_hb_group[attacker][src] != g) {
              continue;
            }
            memcpy(hitlist_hb_cd[attacker][hb_id], prev_cd[src],
                   sizeof(hitlist_hb_cd[attacker][hb_id]));
            memcpy(hitlist_hb_iid[attacker][hb_id], prev_iid[src],
                   sizeof(hitlist_hb_iid[attacker][hb_id]));
            hitlist_hb_authoritative[attacker][hb_id] = hitlist_hb_authoritative[attacker][src];
            copied = 1u;
            break;
          }
          if (!copied) {
            memset(hitlist_hb_cd[attacker][hb_id], 0, sizeof(hitlist_hb_cd[attacker][hb_id]));
            memset(hitlist_hb_iid[attacker][hb_id], 0, sizeof(hitlist_hb_iid[attacker][hb_id]));
            hitlist_hb_authoritative[attacker][hb_id] = 0u;
          }
        }
        if (sim_hitlag[attacker] != 0u) {
          continue;
        }
        for (int victim = 0; victim < num_players; victim++) {
          uint16_t cd = hitlist_hb_cd[attacker][hb_id][victim];
          if (cd == 0u || cd == 0xFFFFu) {
            continue;
          }
          cd = (uint16_t)(cd - 1u);
          hitlist_hb_cd[attacker][hb_id][victim] = cd;
          if (cd == 0u) {
            hitlist_hb_iid[attacker][hb_id][victim] = 0u;
          }
        }
      }
      memcpy(prev_hb_active[attacker], cur_active, sizeof(cur_active));
      memcpy(prev_hb_group[attacker], cur_group, sizeof(cur_group));

      if (!any_hitboxes) {
        continue;
      }

      for (int defender = 0; defender < num_players; defender++) {
        if (defender == attacker) {
          continue;
        }
        const npy_intp di = fi * width + defender;
        if (stocks_p[di] == 0u || hurtbox_state_p[di] == 2u) {
          continue;
        }
        const uint8_t defender_no_damage = hurtbox_state_p[di] != 0u ? 1u : 0u;
        if (is_teams && team_p[ai] == team_p[di]) {
          continue;
        }
        if (sim_hitlag[attacker] != 0u || sim_hitlag[defender] != 0u) {
          continue;
        }
        const uint8_t defender_on_ground = on_ground_p[di] != 0u ? 1u : 0u;
        const uint8_t defender_hitlag_seen = hitlag_p != NULL ? (hitlag_p[di] > 0u ? 1u : 0u) : 1u;
        const uint8_t attacker_hitlag_seen = hitlag_p != NULL ? (hitlag_p[ai] > 0u ? 1u : 0u) : 1u;
        const uint8_t shield_active = shield_r[defender] > 0.0f ? 1u : 0u;
        uint8_t did_hit = 0u;

        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          MslPyHbPrim* hb = &hitboxes[attacker][hb_id];
          if (!hb->valid) {
            continue;
          }
          if (defender_on_ground) {
            if ((hb->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
              continue;
            }
          } else if ((hb->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
            continue;
          }

          uint8_t shield_contact_seed_kind = 0u;
          if (include_replay_only_shield_admission && shield_active && hitlag_p != NULL &&
              fi + 1 < n && msl_py_is_attackair_action(action_p[ai]) && hb->damage > 0.0f &&
              hitlag_p[di] == 0u && hitlag_p[ai] == 0u) {
            const npy_intp ni_a = (fi + 1) * width + attacker;
            const npy_intp ni_d = (fi + 1) * width + defender;
            if (action_p[ni_d] == MSL_ACT_GUARD_SET_OFF && hitlag_p[ni_d] > 0u &&
                hitlag_p[ni_a] > 0u) {
              shield_contact_seed_kind = 2u;
            } else if (hitlag_p[ni_d] == 0u && hitlag_p[ni_a] == 0u) {
              shield_contact_seed_kind = 1u;
            }
          }
          if (shield_contact_seed_kind) {
            const npy_intp oi =
                (((fi * (npy_intp)MSL_MAX_PLAYERS + attacker) * MSL_MAX_HITBOXES + hb_id) *
                 MSL_MAX_PLAYERS) +
                defender;
            out_shield_kind_p[oi] = shield_contact_seed_kind;
          }

          const uint8_t hit_group = hb->group & 0x7u;
          uint8_t prune_guard_stale_seed_bridge = 0u;
          if (hitlag_p != NULL && last_hit_by_p != NULL && instance_hit_by_p != NULL &&
              action_p[di] == MSL_ACT_GUARD && hitlag_p[di] == 0u &&
              last_hit_by_p[di] == (uint8_t)attacker &&
              instance_hit_by_p[di] != instance_id_p[ai] && hb->def_frame == action_frame_p[ai] &&
              action_frame_p[ai] <= 8) {
            prune_guard_stale_seed_bridge = 1u;
          }

          const uint16_t cd = hitlist_cd[attacker][hit_group][defender];
          if (cd != 0u) {
            const uint8_t first_guardsetoff =
                (hitlag_p != NULL && action_p[di] == MSL_ACT_GUARD_SET_OFF && hitlag_p[di] > 0u &&
                 (fi == 0 ||
                  (hitlag_p[(fi - 1) * width + defender] == 0u &&
                   msl_py_is_shield_active_action(action_p[(fi - 1) * width + defender]))))
                    ? 1u
                    : 0u;
            const uint8_t guardsetoff_damage_onset =
                (hitlag_p != NULL && fi > 0 && action_p[di] == MSL_ACT_GUARD_SET_OFF &&
                 hitlag_p[di] > 0u && hitlag_p[(fi - 1) * width + defender] == 0u &&
                 shield_hp_p[di] < shield_hp_p[(fi - 1) * width + defender])
                    ? 1u
                    : 0u;
            if (first_guardsetoff || guardsetoff_damage_onset) {
              const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = seeded;
                  hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                  hitlist_hb_authoritative[attacker][reg] = 1u;
                }
              }
            }
            if (include_replay_only_shield_admission && hitlag_p != NULL && fi + 1 < n &&
                msl_py_is_attackair_action(action_p[ai]) && hb->damage > 0.0f &&
                hitlag_p[di] == 0u && hitlag_p[ai] == 0u &&
                action_p[(fi + 1) * width + defender] == MSL_ACT_GUARD_SET_OFF &&
                hitlag_p[(fi + 1) * width + defender] > 0u &&
                hitlag_p[(fi + 1) * width + attacker] > 0u) {
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = 0u;
                  hitlist_hb_iid[attacker][reg][defender] = 0u;
                  replay_only_hb_valid_frame[attacker][reg] = 1u;
                }
              }
            } else if (include_replay_only_body_admission && hitlag_p != NULL &&
                       percent_p != NULL && fi + 1 < n && hb->damage > 0.0f && hitlag_p[di] == 0u &&
                       hitlag_p[ai] == 0u && hitlag_p[(fi + 1) * width + defender] > 0u &&
                       hitlag_p[(fi + 1) * width + attacker] > 0u &&
                       percent_p[(fi + 1) * width + defender] > percent_p[di] &&
                       (last_hit_by_p == NULL ||
                        last_hit_by_p[(fi + 1) * width + defender] == (uint8_t)attacker) &&
                       (instance_hit_by_p == NULL ||
                        instance_hit_by_p[(fi + 1) * width + defender] == instance_id_p[ai])) {
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = 0u;
                  hitlist_hb_iid[attacker][reg][defender] = 0u;
                  replay_only_hb_valid_frame[attacker][reg] = 1u;
                }
              }
            } else {
              const uint16_t def_iid = instance_id_p[di];
              if (hitlist_iid[attacker][hit_group][defender] == def_iid) {
                if (prune_guard_stale_seed_bridge) {
                  hitlist_cd[attacker][hit_group][defender] = 0u;
                  hitlist_iid[attacker][hit_group][defender] = 0u;
                } else {
                  continue;
                }
              } else if (prune_guard_stale_seed_bridge) {
                hitlist_cd[attacker][hit_group][defender] = 0u;
                hitlist_iid[attacker][hit_group][defender] = 0u;
              } else if (msl_py_hitlist_victim_pointer_may_change(stocks_p[di], action_p[di])) {
                hitlist_cd[attacker][hit_group][defender] = 0u;
                hitlist_iid[attacker][hit_group][defender] = 0u;
              } else {
                hitlist_iid[attacker][hit_group][defender] = def_iid;
                continue;
              }
            }
          }

          const uint8_t shield_contact =
              shield_active && (shield_contact_seed_kind == 2u ||
                                (shield_contact_seed_kind != 1u &&
                                 msl_py_sphere_sphere_intersects(
                                     hb->x, hb->y, hb->z, hb->r, shield_x[defender],
                                     shield_y[defender], shield_z[defender], shield_r[defender])))
                  ? 1u
                  : 0u;
          if (shield_contact) {
            if (hb->damage > 0.0f && defender_hitlag_seen) {
              const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = seeded;
                  hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                  hitlist_hb_authoritative[attacker][reg] = 1u;
                }
              }
              hitlist_cd[attacker][hit_group][defender] = seeded;
              hitlist_iid[attacker][hit_group][defender] = instance_id_p[di];
              const uint16_t hl = msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
              sim_hitlag[attacker] = hl;
              sim_hitlag[defender] = hl;
              did_hit = 1u;
              break;
            }
            continue;
          }

          if (cap_counts[defender] == 0u) {
            continue;
          }
          for (uint16_t ci = 0; ci < cap_counts[defender]; ci++) {
            MslPyCapPrim* cap = &caps[defender][ci];
            if (!cap->valid ||
                !msl_py_sphere_capsule_intersects(hb->x, hb->y, hb->z, hb->r, cap->ax, cap->ay,
                                                  cap->az, cap->bx, cap->by, cap->bz, cap->r)) {
              continue;
            }
            const uint8_t no_damage_contact_seen =
                (defender_no_damage && attacker_hitlag_seen) ? 1u : 0u;
            if (!defender_hitlag_seen && !no_damage_contact_seen) {
              if (!(include_replay_only_body_admission && hitlag_p != NULL && percent_p != NULL &&
                    fi + 1 < n && hitlag_p[di] == 0u && hitlag_p[ai] == 0u &&
                    hitlag_p[(fi + 1) * width + defender] > 0u &&
                    hitlag_p[(fi + 1) * width + attacker] > 0u &&
                    percent_p[(fi + 1) * width + defender] > percent_p[di] &&
                    (last_hit_by_p == NULL ||
                     last_hit_by_p[(fi + 1) * width + defender] == (uint8_t)attacker) &&
                    (instance_hit_by_p == NULL ||
                     instance_hit_by_p[(fi + 1) * width + defender] == instance_id_p[ai]))) {
                continue;
              }
              if (pending_count < (int)(sizeof(pending) / sizeof(pending[0]))) {
                pending[pending_count].attacker = attacker;
                pending[pending_count].group = hit_group;
                pending[pending_count].defender = defender;
                pending[pending_count].rehit = hb->rehit;
                pending[pending_count].iid = instance_id_p[(fi + 1) * width + defender];
                pending[pending_count].hl =
                    msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
                pending_count++;
              }
              did_hit = 1u;
              break;
            }
            const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
            for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
              if (hitboxes[attacker][reg].valid &&
                  (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                hitlist_hb_cd[attacker][reg][defender] = seeded;
                hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                hitlist_hb_authoritative[attacker][reg] = 1u;
              }
            }
            hitlist_cd[attacker][hit_group][defender] = seeded;
            hitlist_iid[attacker][hit_group][defender] = instance_id_p[di];
            const uint16_t hl = msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
            if (defender_no_damage) {
              if (hitlag_p != NULL) {
                sim_hitlag[attacker] = hitlag_p[ai];
              }
            } else {
              sim_hitlag[attacker] = hl;
              sim_hitlag[defender] = hl;
            }
            did_hit = 1u;
            break;
          }
          if (did_hit) {
            break;
          }
        }
      }
    }

    for (int attacker = 0; attacker < num_players; attacker++) {
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (!hitboxes[attacker][hb_id].valid) {
          continue;
        }
        const npy_intp oi = (fi * (npy_intp)MSL_MAX_PLAYERS + attacker) * MSL_MAX_HITBOXES + hb_id;
        out_hb_valid_p[oi] = replay_only_hb_valid_frame[attacker][hb_id]
                                 ? 1u
                                 : (hitlist_hb_authoritative[attacker][hb_id] ? 1u : 0u);
      }
    }

    memcpy(out_cd_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS,
           hitlist_cd, sizeof(hitlist_cd));
    memcpy(out_iid_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS,
           hitlist_iid, sizeof(hitlist_iid));
    memcpy(out_hb_cd_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS,
           hitlist_hb_cd, sizeof(hitlist_hb_cd));
    memcpy(out_hb_iid_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS,
           hitlist_hb_iid, sizeof(hitlist_hb_iid));

    for (int pi = 0; pi < pending_count; pi++) {
      const uint16_t seeded = pending[pi].rehit == 0 ? 0xFFFFu : (uint16_t)pending[pi].rehit;
      for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
        if (hitboxes[pending[pi].attacker][reg].valid &&
            (hitboxes[pending[pi].attacker][reg].group & 0x7u) == (uint8_t)pending[pi].group) {
          hitlist_hb_cd[pending[pi].attacker][reg][pending[pi].defender] = seeded;
          hitlist_hb_iid[pending[pi].attacker][reg][pending[pi].defender] =
              (uint16_t)pending[pi].iid;
          hitlist_hb_authoritative[pending[pi].attacker][reg] = 1u;
        }
      }
      hitlist_cd[pending[pi].attacker][pending[pi].group][pending[pi].defender] = seeded;
      hitlist_iid[pending[pi].attacker][pending[pi].group][pending[pi].defender] =
          (uint16_t)pending[pi].iid;
      sim_hitlag[pending[pi].attacker] = (uint16_t)pending[pi].hl;
      sim_hitlag[pending[pi].defender] = (uint16_t)pending[pi].hl;
    }
  }

  return Py_BuildValue("NNNNNN", out_cd, out_iid, out_hb_valid, out_hb_cd, out_hb_iid,
                       out_shield_kind);
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
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
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
    {"debug_refresh_combat_geometry", msl_debug_refresh_combat_geometry, METH_VARARGS,
     "debug_refresh_combat_geometry(handle) -> DEBUG-ONLY. Recompute hurtcaps/hitboxes from "
     "current state without advancing frame stages."},
    {"write_compare", msl_write_compare, METH_VARARGS, "write_compare(handle, out_bytes)"},
    {"write_rl_observation", msl_write_rl_observation, METH_VARARGS,
     "write_rl_observation(handle, viewpoint_players[batch], out_bytes)"},
    {"write_terminal", msl_write_terminal, METH_VARARGS,
     "write_terminal(handle, out_bytes, max_frame_id=-1)"},
    {"debug_write_processed_input", msl_debug_write_processed_input, METH_VARARGS,
     "debug_write_processed_input(handle, out_bytes)"},
    {"debug_write_internals", msl_debug_write_internals, METH_VARARGS,
     "debug_write_internals(handle, out_bytes)"},
    {"debug_write_collision_contacts", msl_debug_write_collision_contacts, METH_VARARGS,
     "debug_write_collision_contacts(handle, out_bytes)"},
    {"debug_force_anim_timebase_enter", msl_debug_force_anim_timebase_enter, METH_VARARGS,
     "debug_force_anim_timebase_enter(handle, batch_index, player_index, anim_start, anim_speed)"},
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
    {"debug_dynamic_pose_state", msl_debug_dynamic_pose_state_py, METH_VARARGS,
     "debug_dynamic_pose_state(handle, batch_index, player_index) -> "
     "bytes[1,sizeof(MslDebugDynamicPoseState)]"},
    {"debug_attackairb_continuation_overlap", msl_debug_attackairb_continuation_overlap_py,
     METH_VARARGS,
     "debug_attackairb_continuation_overlap(handle, batch_index, attacker, hb_id, defender, "
     "cap_id) -> float"},
    {"debug_body_matrix_overlap", msl_debug_body_matrix_overlap_py, METH_VARARGS,
     "debug_body_matrix_overlap(handle, batch_index, attacker, hb_id, defender, cap_id) -> float"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
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
    {"stage_match_flow_roles", msl_stage_match_flow_roles_py, METH_VARARGS,
     "stage_match_flow_roles(stage_id) -> dict of runtime MSLSTG01 match-flow role data."},
    {"hitlist_ring_demo", msl_hitlist_ring_demo_py, METH_VARARGS,
     "hitlist_ring_demo(inserts) -> (ring, ids_u32[12]) (test-only)"},
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
    PyModuleDef_HEAD_INIT, "msl_binding", NULL, -1, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit_msl_binding(void) {
  import_array();
  return PyModule_Create(&moduledef);
}
