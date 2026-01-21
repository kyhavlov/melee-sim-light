#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <numpy/arrayobject.h>

#include "../src/alloc.h"
#include "../src/api.h"
#include "../src/anim_pose.h"
#include "../src/ecb_tables.h"

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

static PyObject* msl_init(PyObject* self, PyObject* args, PyObject* kwargs) {
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

static PyObject* msl_step_input(PyObject* self, PyObject* args) {
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

static PyObject* msl_write_compare(PyObject* self, PyObject* args) {
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

static PyObject* msl_debug_write_processed_input(PyObject* self, PyObject* args) {
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

static PyObject* msl_sizes(PyObject* self, PyObject* args) {
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i}", "seed", (int)sizeof(MslSeed), "input",
                       (int)sizeof(MslInput), "compare", (int)sizeof(MslCompare), "sample",
                       (int)sizeof(MslSample), "processed_input", (int)sizeof(MslProcessedInput),
                       "internals", (int)sizeof(MslDebugInternals));
}

static PyObject* msl_alloc_reset(PyObject* self, PyObject* args) {
  msl_alloc_reset_counters();
  Py_RETURN_NONE;
}

static PyObject* msl_alloc_stats(PyObject* self, PyObject* args) {
  const unsigned long long calls = (unsigned long long)msl_alloc_total_calls();
  const unsigned long long bytes = (unsigned long long)msl_alloc_total_bytes();
  return Py_BuildValue("{s:K,s:K}", "calls", calls, "bytes", bytes);
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

  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOiif", &rest_rot_obj, &rest_pos_obj, &rest_scl_obj,
                        &parent_part_obj, &part_flags_obj, &order_obj, &local_parts_obj,
                        &joint_parts_obj, &update_parts_obj, &fobj_starts_obj, &fobj_desc_obj,
                        &ad_source_obj, &frame_count, &inv_scale_part, &inv_model_scale)) {
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

static PyMethodDef methods[] = {
    {"init", (PyCFunction)msl_init, METH_VARARGS | METH_KEYWORDS,
     "init(batch_size, num_players, ucf_enabled=?, ucf_cardinals_1_0_enabled=?) -> handle"},
    {"destroy", msl_destroy, METH_VARARGS,
     "destroy(handle) -> None (free underlying C batch immediately)"},
    {"reseed_seed", msl_reseed_seed, METH_VARARGS,
     "reseed_seed(handle, seed_bytes[batch, seed_stride])"},
    {"step_input", msl_step_input, METH_VARARGS,
     "step_input(handle, prev_input_bytes, input_bytes)"},
    {"write_compare", msl_write_compare, METH_VARARGS, "write_compare(handle, out_bytes)"},
    {"debug_write_processed_input", msl_debug_write_processed_input, METH_VARARGS,
     "debug_write_processed_input(handle, out_bytes)"},
    {"debug_write_internals", msl_debug_write_internals, METH_VARARGS,
     "debug_write_internals(handle, out_bytes)"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
    {"alloc_reset", msl_alloc_reset, METH_NOARGS,
     "Reset C allocation counters (debug/perf guardrail)."},
    {"alloc_stats", msl_alloc_stats, METH_NOARGS,
     "Get C allocation counters (debug/perf guardrail)."},
    {"ecb_bottom_rel_y", msl_ecb_bottom_rel_y_py, METH_VARARGS,
     "ecb_bottom_rel_y(char_id, animation_index, action_frame) -> float"},
    {"ecb_extents_rel", msl_ecb_extents_rel_py, METH_VARARGS,
     "ecb_extents_rel(char_id, animation_index, action_frame) -> (min_x, max_x, min_y, max_y)"},
    {"anim_pose_matrix", msl_anim_pose_matrix_py, METH_VARARGS,
     "anim_pose_matrix(char_id, msid, frame, part_id) -> np.ndarray[float32] shape=(12,)"},
    {"hurtcaps_world", msl_hurtcaps_world_py, METH_VARARGS,
     "hurtcaps_world(handle, batch_index, player_index) -> (caps[MSL_MAX_HURTCAPS,7], count)"},
    {"hitboxes_world", msl_hitboxes_world_py, METH_VARARGS,
     "hitboxes_world(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,10], count)"},
    {"hitboxes_world_full", msl_hitboxes_world_full_py, METH_VARARGS,
     "hitboxes_world_full(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,16], count)"},
    {"debug_combat_contacts", msl_debug_combat_contacts_py, METH_VARARGS,
     "debug_combat_contacts(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_combat_contacts_filtered", msl_debug_combat_contacts_filtered_py, METH_VARARGS,
     "debug_combat_contacts_filtered(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_clear_hitboxes_world", msl_debug_clear_hitboxes_world_py, METH_VARARGS,
     "debug_clear_hitboxes_world(handle, batch_index, player_index)"},
    {"debug_set_hitbox_world", msl_debug_set_hitbox_world_py, METH_VARARGS,
     "debug_set_hitbox_world(handle, batch_index, player_index, hitbox_id, x,y,z,radius,damage, "
     "enabled=1)"},
    {"debug_set_hitbox_flags", msl_debug_set_hitbox_flags_py, METH_VARARGS,
     "debug_set_hitbox_flags(handle, batch_index, player_index, hitbox_id, hitbox_flags_u16)"},
    {"debug_clear_hurtcaps_world", msl_debug_clear_hurtcaps_world_py, METH_VARARGS,
     "debug_clear_hurtcaps_world(handle, batch_index, player_index)"},
    {"debug_set_hurtcap_world", msl_debug_set_hurtcap_world_py, METH_VARARGS,
     "debug_set_hurtcap_world(handle, batch_index, player_index, hurtcap_id, "
     "ax,ay,az,bx,by,bz,radius)"},
    {"debug_combat_resolve", msl_debug_combat_resolve_py, METH_VARARGS,
     "debug_combat_resolve(handle) -> run combat_resolve() only"},
    {"debug_point_segment_dist2", msl_debug_point_segment_dist2_py, METH_VARARGS,
     "debug_point_segment_dist2(px,py,pz, ax,ay,az, bx,by,bz) -> (dist2, t)"},
    {"anim_bake_ssanim01", msl_anim_bake_ssanim01_py, METH_VARARGS,
     "anim_bake_ssanim01(rest_rot, rest_pos, rest_scl, parent_part, part_flags, order, "
     "local_parts, joint_parts, "
     "update_parts, fobj_starts, fobj_desc, ad_source, frame_count, inv_scale_part, "
     "inv_model_scale) -> "
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
