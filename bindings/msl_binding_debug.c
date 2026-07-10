#include "msl_binding_debug.h"
#include "msl_binding_internal.h"

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
    return 6;
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
  // HSD_FObjInterpretAnim keeps helper EOF returns in the interpreter-local state machine. The
  // data/wait EOF helpers return 6 to finish this interpret call, but source does not persist
  // `fo->state = 6` at those EOF branches.
  // refs/melee/src/sysdolphin/baselib/fobj.c::{FObjLoadData,FObjLoadWait,HSD_FObjInterpretAnim}
  // refs/melee/build/GALE01/asm/sysdolphin/baselib/fobj.s
  int state = fo->state;
  int iters = 0;
  while (true) {
    iters += 1;
    if (iters > 100000) {
      if (any) {
        *out_value = last;
      }
      return any;
    }
    const int st = state;
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
      state = fobj_load_data(fo);
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
      state = fobj_load_wait(fo);
      continue;
    }
    if (st == 4) {
      if ((float)fo->fterm <= fo->time) {
        fterm = (float)fo->fterm;
        fo->time = f32_from_double((double)fo->time - (double)fo->fterm);
        fo->state = FOBJ_LOAD_WAIT;
        state = FOBJ_LOAD_WAIT;
        continue;
      }
      float v = 0.0f;
      if (fobj_update_anim(fo, &v)) {
        last = v;
        any = true;
      }
      fo->state = 5;
      state = 5;
      *out_value = last;
      return any;
    }
    if (st == 5) {
      fo->state = 4;
      state = 4;
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

PyObject* msl_debug_copy_lanes_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_copy_colldata_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_step_input_pre_combat(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_knockdown_update_pre_physics(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_coll_env_flags_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_mpcoll_joint_filters_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_escapeair_floor_producer_runtime_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_floor_sweep_prev_runtime_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_wall_ceil_prev_runtime_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_player_root_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_ceiling_contact_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_run_knockdown_post_collision_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_run_locomotion_post_collision_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_refresh_combat_geometry(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_timebase_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hitbox_event_timing_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hitbox_sweep_proxy_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hurtcap_slot_flags_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hurtcap_geometry_valid_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hurtcap_matrix_valid_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_poison_hurtcap_matrix_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_dynamic_pose_state_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_common_fall_blend_state_py(PyObject* self, PyObject* args) {
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
  if (batch_index < 0 || batch_index >= h->batch->batch_size || player_index < 0 ||
      player_index >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "batch_index/player_index out of range");
    return NULL;
  }
  const size_t idx = (size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)player_index;
  return Py_BuildValue("dI", (double)h->batch->state.common_fall_blend_x4[idx],
                       (unsigned int)h->batch->state.common_fall_blend_msid[idx]);
}

PyObject* msl_debug_get_fighter_8006cda4_pre_gate_consume_count_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_get_sheik_needle_count_py(PyObject* self, PyObject* args) {
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
  const int err =
      msl_batch_debug_get_sheik_needle_count(h->batch, batch_index, player_index, &count);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_get_sheik_needle_count failed: %d", err);
    return NULL;
  }
  return PyLong_FromUnsignedLong((unsigned long)count);
}

PyObject* msl_debug_attackairb_continuation_overlap_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_body_matrix_overlap_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_write_processed_input(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_write_stage_state(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_write_internals(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_write_collision_contacts(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_write_colldata_ecb(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_force_anim_timebase_enter(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_reset_pose_and_hitboxes_tables_py(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  const int err = msl_debug_reset_pose_and_hitboxes_tables();
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_debug_reset_pose_and_hitboxes_tables failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_ecb_bottom_rel_y_py(PyObject* self, PyObject* args) {
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

PyObject* msl_ecb_extents_rel_py(PyObject* self, PyObject* args) {
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

PyObject* msl_anim_pose_matrix_py(PyObject* self, PyObject* args) {
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

PyObject* msl_anim_pose_common_fall_blend_matrix_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned int neutral_msid_u = 0;
  unsigned int target_msid_u = 0;
  double anim_frame = 0.0;
  unsigned int part_id_u = 0;
  double weight = 0.0;
  if (!PyArg_ParseTuple(args, "IIIdId", &char_id_u, &neutral_msid_u, &target_msid_u, &anim_frame,
                        &part_id_u, &weight)) {
    return NULL;
  }
  if (char_id_u > 255u || neutral_msid_u > 0xFFFFu || target_msid_u > 0xFFFFu ||
      part_id_u > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "char_id/msid/part_id out of range");
    return NULL;
  }

  npy_intp dims[1] = {(npy_intp)12};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = anim_pose_debug_common_fall_blend_matrix(
      (uint8_t)char_id_u, (uint16_t)neutral_msid_u, (uint16_t)target_msid_u, (float)anim_frame,
      (uint16_t)part_id_u, (float)weight, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_SetString(PyExc_ValueError, "anim_pose_debug_common_fall_blend_matrix failed");
    return NULL;
  }
  return (PyObject*)arr;
}

PyObject* msl_anim_pose_collision_matrix_f32_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned int msid_u = 0;
  double anim_frame = 0.0;
  unsigned int part_id_u = 0;
  if (!PyArg_ParseTuple(args, "IIdI", &char_id_u, &msid_u, &anim_frame, &part_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u || msid_u > 0xFFFFu || part_id_u > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "char_id/msid/part_id out of range");
    return NULL;
  }

  npy_intp dims[1] = {(npy_intp)12};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = anim_pose_debug_collision_matrix_f32((uint8_t)char_id_u, (uint16_t)msid_u,
                                                       (float)anim_frame, (uint16_t)part_id_u, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_SetString(PyExc_ValueError, "anim_pose_debug_collision_matrix_f32 failed");
    return NULL;
  }
  return (PyObject*)arr;
}

PyObject* msl_hurtcaps_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_hitboxes_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_hitboxes_world_full_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_contacts_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_contacts_filtered_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_select_body_hits_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_contacts_classified_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_contacts_classified_filtered_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_shield_candidate_decisions_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_shield_bubbles_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_clear_hitboxes_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_flags_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_group_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_enable_edge_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_element_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hitbox_kb_params_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_rollout_clock_mode_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_get_rollout_clock_mode_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_camera_mode_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_clear_hurtcaps_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hurtcap_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hurtcap_height_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hurtcap_enabled_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_combat_resolve_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_run_item_collision_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_run_item_collision_phase(h->batch);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_run_item_collision_phase failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_debug_set_hitlag_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_sheik_vanish_smoke_accessory_pending_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  int pending = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &handle_obj, &batch_index, &player_index, &pending)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int err = msl_batch_debug_set_sheik_vanish_smoke_accessory_pending(
      h->batch, batch_index, player_index, (uint8_t)(pending ? 1u : 0u));
  if (err != 0) {
    PyErr_Format(PyExc_ValueError,
                 "msl_batch_debug_set_sheik_vanish_smoke_accessory_pending failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_debug_set_damage_source_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_damage_phase_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_phantom_damage_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_prev_action_id_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_grab_owner_port_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  unsigned int grab_owner_port = 0;
  if (!PyArg_ParseTuple(args, "OiiI", &handle_obj, &batch_index, &player_index, &grab_owner_port)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (grab_owner_port > 0xFFu) {
    PyErr_SetString(PyExc_ValueError, "grab_owner_port out of range");
    return NULL;
  }
  const int err = msl_batch_debug_set_grab_owner_port(h->batch, batch_index, player_index,
                                                      (uint8_t)grab_owner_port);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_set_grab_owner_port failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_debug_set_smash_charge_state_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_set_hit_status_override_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_point_segment_dist2_py(PyObject* self, PyObject* args) {
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

PyObject* msl_pose_points_world_py(PyObject* self, PyObject* args) {
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

PyObject* msl_anim_bake_ssanim01_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hitlist_fighter_contains_py(PyObject* self, PyObject* args) {
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

PyObject* msl_debug_hitlist_item_contains_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* capsule = NULL;
  int batch_index = 0;
  int item_slot = 0;
  int hb_id = 0;
  int victim = 0;
  if (!PyArg_ParseTuple(args, "Oiiii", &capsule, &batch_index, &item_slot, &hb_id, &victim)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid handle");
    return NULL;
  }

  int present = 0;
  const int err = msl_batch_debug_hitlist_item_contains(h->batch, batch_index, item_slot, hb_id,
                                                        victim, &present);
  if (err != 0) {
    PyErr_Format(PyExc_ValueError, "msl_batch_debug_hitlist_item_contains failed: %d", err);
    return NULL;
  }
  return PyLong_FromLong((long)present);
}

PyObject* msl_debug_hitlist_fighter_capsule_py(PyObject* self, PyObject* args) {
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

PyObject* msl_hitlist_ring_demo_py(PyObject* self, PyObject* args) {
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

PyObject* msl_hitlist_insert_cd_demo_py(PyObject* self, PyObject* args) {
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
