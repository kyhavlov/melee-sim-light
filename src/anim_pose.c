#include "anim_pose.h"
#include "data_dir.h"
#include "action_ids.h"
#include "char_registry.h"
#include "ids.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "anim_frame.h"
#include "batch_internal.h"
#include "msl_math.h"

// SSANIM01 v5 is written by tools/extraction/extract_fighter_anims.py.
enum {
  ANIM_MAGIC_LEN = 8,
  ANIM_HDR_BASE_BYTES = 16,  // magic[8] + ver[u32] + joint_count[u16] + anim_count[u16]
  ANIM_VERSION_V5 = 5,
  ANIM_DYN_VERSION_V4 = 4,
  ANIM_DYN_VERSION_V5 = 5,
  ANIM_DYN_VERSION_V6 = 6,
  ANIM_DYN_VERSION_V7 = 7,
  ANIM_DYN_VERSION_V8 = 8,
  MAT_BYTES = 12 * 4,              // float32[12] (3x4)
  TRANSN_BYTES_PER_FRAME = 3 * 4,  // float32[3] v4 tail (TransN/root translation)

  // SSDYNN01 v8 is written by tools/extraction/extract_fighter_anims.py.
  //
  // RL1.0 target data contract:
  // - Fox ftData.x2C has exactly one dynamic bone set rooted at part 17.
  // - Falco ftData.x2C has zero dynamic bone sets.
  //
  // Runtime state is indexed by player and node. Until a multi-set seed/state surface is added,
  // reject present SSDYNN01 files with more than one set instead of accepting a layout the hot path
  // cannot represent without set-index collisions.
  ANIM_DYN_MAX_SETS = 1,
  ANIM_DYN_MAX_COLLIDERS = 4
};

static const float kDynColliderSkinRadius = 0.1f;  // lb_00F9.s::lb_804D7BE0

static const uint8_t k_anim_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', '0', '1'};
typedef struct {
  uint16_t part_id;
  float c[15];
} MslAnimDynNodeData;

typedef struct {
  uint16_t root_part;
  uint16_t node_count;
  float pos[3];
  MslAnimDynNodeData nodes[MSL_MAX_DYNAMIC_NODES];
} MslAnimDynSetData;

typedef struct {
  uint16_t part_id;
  float offset[3];
  float radius;
} MslAnimDynColliderData;

typedef struct {
  float x;
  float y;
  float z;
  float w;
} MslQuat;

typedef struct {
  uint8_t* buf;
  size_t sz;

  // Header-derived.
  uint16_t joint_count;
  uint16_t anim_count;

  // O(1) lookup tables (bounded, init-time allocated):
  // - msid -> (have, frame_count, base_offset)
  // - part_id -> joint_index (0xFFFF if missing)
  uint8_t* have_msid;             // [65536]
  uint16_t* frame_count_by_msid;  // [65536]
  uint32_t* base_off_by_msid;     // [65536] byte offset to frame0/joint0 matrices
  uint16_t* part_to_joint_index;  // [65536]

  uint8_t* local_buf;
  size_t local_sz;
  uint16_t local_count;
  uint16_t local_anim_count;
  uint16_t* local_part_to_index;        // [65536]
  int16_t* local_parent_part_by_index;  // [local_count]
  uint32_t* local_flags_by_index;       // [local_count]
  uint8_t* local_have_msid;             // [65536]
  uint16_t* local_frame_count_by_msid;  // [65536]
  uint32_t* local_base_off_by_msid;     // [65536] byte offset to frame0/local0 SRT records

  uint16_t dyn_set_count;
  uint16_t dyn_total_nodes;
  uint16_t dyn_collider_count;
  MslAnimDynSetData dyn_sets[ANIM_DYN_MAX_SETS];
  MslAnimDynColliderData dyn_colliders[ANIM_DYN_MAX_COLLIDERS];
  uint8_t* dyn_collision_have_msid;        // [65536], extracted SSDYNN01 collision-owner index
  uint8_t* dyn_cone_have_msid;             // [65536], extracted descriptor +0x68 cone-owner index
  uint8_t* dyn_catch_grabbable_have_msid;  // [65536], extracted Catch grabbable owner index

  uint8_t* track_buf;
  size_t track_sz;
  uint16_t track_local_count;
  uint16_t track_anim_count;
  uint16_t* track_part_to_index;               // [65536]
  uint16_t* track_msid_to_anim_index;          // [65536], 0xFFFF if missing
  uint32_t* track_part_record_off_by_anim_li;  // [track_anim_count * track_local_count]
  uint32_t* exact_local_base_by_msid;          // [65536], float offset or UINT32_MAX
  float* exact_integer_locals;                 // source-interpreted common-Fall local SRTs

  uint8_t have;
} MslAnimPoseTable;

static MslAnimPoseTable g_table_by_char[256];
static int g_loaded = 0;

static int build_exact_integer_local_cache(MslAnimPoseTable* t);

uint32_t anim_pose_data_schema_version(void) { return 2u; }

static uint16_t read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint32_t read_u32_le(const uint8_t* p) {
  uint32_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static float read_f32_le(const uint8_t* p) {
  float v = 0.0f;
  memcpy(&v, p, sizeof(v));
  return v;
}

static inline float f32_from_double(double x) { return (float)x; }
static inline float f32_mul_d(double a, double b) { return (float)(a * b); }
static inline float f32_madd_d(double a, double b, double c) { return (float)(a * b + c); }

static float spl_get_helmite_f32(float fterm, float time, float p0, float p1, float d0, float d1) {
  // Exact instruction order from `refs/melee/build/GALE01/asm/sysdolphin/baselib/spline.s`
  // `splGetHelmite`, shared with the extraction/native bake path.
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
  uint8_t flags_20;
  uint8_t flags_40;
  uint8_t flags_80;
  uint8_t parse_error;
  int pos;
} MslFObjEval;

static float fobj_parse_float(const uint8_t* ad, int len, int* pos, uint8_t frac,
                              uint8_t* parse_error) {
  const int p = *pos;
  if ((frac & 0xE0u) == HSD_A_FRAC_FLOAT) {
    if (p + 4 > len) {
      if (parse_error != NULL) {
        *parse_error = 1u;
      }
      return 0.0f;
    }
    const uint32_t d = (uint32_t)ad[p] | ((uint32_t)ad[p + 1] << 8) | ((uint32_t)ad[p + 2] << 16) |
                       ((uint32_t)ad[p + 3] << 24);
    union {
      uint32_t u;
      float f;
    } v;
    v.u = d;
    *pos = p + 4;
    return v.f;
  }

  const int denom = 1 << (frac & 0x1Fu);
  const uint8_t kind = frac & 0xE0u;
  if (kind == HSD_A_FRAC_S8) {
    if (p + 1 > len) {
      if (parse_error != NULL) {
        *parse_error = 1u;
      }
      return 0.0f;
    }
    const int8_t numer = (int8_t)ad[p];
    *pos = p + 1;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_U8) {
    if (p + 1 > len) {
      if (parse_error != NULL) {
        *parse_error = 1u;
      }
      return 0.0f;
    }
    const uint8_t numer = ad[p];
    *pos = p + 1;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_S16) {
    if (p + 2 > len) {
      if (parse_error != NULL) {
        *parse_error = 1u;
      }
      return 0.0f;
    }
    const int16_t numer = (int16_t)((uint16_t)ad[p] | ((uint16_t)ad[p + 1] << 8));
    *pos = p + 2;
    return f32_from_double((double)numer / (double)denom);
  }
  if (kind == HSD_A_FRAC_U16) {
    if (p + 2 > len) {
      if (parse_error != NULL) {
        *parse_error = 1u;
      }
      return 0.0f;
    }
    const uint16_t numer = (uint16_t)ad[p] | ((uint16_t)ad[p + 1] << 8);
    *pos = p + 2;
    return f32_from_double((double)numer / (double)denom);
  }
  return 0.0f;
}

static int fobj_parse_pack_info(const uint8_t* ad, int len, int* pos, int* out_nb_pack) {
  int p = *pos;
  if (p >= len) {
    return -1;
  }
  int d = (int)ad[p++];
  int nb_pack = ((d >> 4) & 7) + 1;
  int shift = 3;
  if ((d & 0x80) == 0) {
    *pos = p;
    *out_nb_pack = nb_pack;
    return 0;
  }
  for (;;) {
    if (p >= len || shift >= 31) {
      return -1;
    }
    d = (int)ad[p++];
    nb_pack += (d & 0x7F) << shift;
    shift += 7;
    if ((d & 0x80) == 0) {
      break;
    }
  }
  *pos = p;
  *out_nb_pack = nb_pack;
  return 0;
}

static int fobj_parse_wait(const uint8_t* ad, int len, int* pos, int* out_wait) {
  int p = *pos;
  int wait = 0;
  int shift = 0;
  for (;;) {
    if (p >= len || shift >= 31) {
      return -1;
    }
    const int d = (int)ad[p++];
    wait |= (d & 0x7F) << shift;
    shift += 7;
    if ((d & 0x80) == 0) {
      break;
    }
  }
  *pos = p;
  *out_wait = wait;
  return 0;
}

static void fobj_req_anim(MslFObjEval* fo, float frame) {
  fo->pos = 0;
  fo->time = f32_from_double((double)fo->startframe + (double)frame);
  fo->op = 0;
  fo->op_intrp = 0;
  fo->flags_20 = 0u;
  fo->flags_40 = 0u;
  fo->flags_80 = 0u;
  fo->nb_pack = 0;
  fo->fterm = 0;
  fo->p0 = 0.0f;
  fo->p1 = 0.0f;
  fo->d0 = 0.0f;
  fo->d1 = 0.0f;
  fo->state = FOBJ_LOAD_DATA0;
}

static void fobj_launch_key_data(MslFObjEval* fo) {
  if (fo->flags_40) {
    fo->op_intrp = fo->op;
    fo->flags_40 = 0u;
    fo->flags_80 = 1u;
    fo->p0 = fo->p1;
  }
}

static int fobj_anim_load(MslFObjEval* fo, int op) {
  if (op == HSD_A_OP_CON || op == HSD_A_OP_LIN) {
    fo->p0 = fo->p1;
    fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value, &fo->parse_error);
    if (fo->op_intrp != HSD_A_OP_SLP) {
      fo->d0 = fo->d1;
      fo->d1 = 0.0f;
    }
    return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
  }
  if (op == HSD_A_OP_SPL0) {
    fo->p0 = fo->p1;
    fo->d0 = fo->d1;
    fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value, &fo->parse_error);
    fo->d1 = 0.0f;
    return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
  }
  if (op == HSD_A_OP_SPL) {
    fo->p0 = fo->p1;
    fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value, &fo->parse_error);
    fo->d0 = fo->d1;
    fo->d1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_slope, &fo->parse_error);
    return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
  }
  if (op == HSD_A_OP_SLP) {
    fo->d0 = fo->d1;
    fo->d1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_slope, &fo->parse_error);
    return fo->state;
  }
  if (op == HSD_A_OP_KEY) {
    fobj_launch_key_data(fo);
    fo->p1 = fobj_parse_float(fo->ad, fo->length, &fo->pos, fo->frac_value, &fo->parse_error);
    fo->flags_40 = 1u;
    return (fo->state == FOBJ_LOAD_DATA0) ? FOBJ_LOAD_WAIT : 4;
  }
  return 0;
}

static int fobj_load_data(MslFObjEval* fo) {
  if (fo->pos >= fo->length) {
    return 6;
  }
  fo->op_intrp = fo->op;
  if (fo->nb_pack == 0) {
    fo->op = (int)(fo->ad[fo->pos] & 0xFu);
    if (fobj_parse_pack_info(fo->ad, fo->length, &fo->pos, &fo->nb_pack) != 0) {
      fo->parse_error = 1u;
      fo->state = 0;
      return fo->state;
    }
  }
  fo->nb_pack -= 1;
  fo->state = fobj_anim_load(fo, fo->op);
  if (fo->parse_error) {
    fo->state = 0;
  }
  return fo->state;
}

static int fobj_load_wait(MslFObjEval* fo) {
  if (fo->pos >= fo->length) {
    return 6;
  }
  if (fobj_parse_wait(fo->ad, fo->length, &fo->pos, &fo->fterm) != 0) {
    fo->parse_error = 1u;
    fo->state = 0;
    return fo->state;
  }
  fo->flags_20 = 1u;
  fo->state = FOBJ_LOAD_DATA;
  return fo->state;
}

static uint8_t fobj_update_anim(MslFObjEval* fo, float* out_value) {
  if (fo->op_intrp == HSD_A_OP_KEY) {
    if (fo->flags_80) {
      fo->flags_80 = 0u;
      *out_value = fo->p0;
      return 1u;
    }
    return 0u;
  }
  if (fo->op_intrp == HSD_A_OP_CON) {
    *out_value = (fo->time >= (float)fo->fterm) ? fo->p1 : fo->p0;
    return 1u;
  }
  if (fo->op_intrp == HSD_A_OP_LIN) {
    if (fo->flags_20) {
      fo->flags_20 = 0u;
      if (fo->fterm != 0) {
        fo->d0 = f32_from_double(((double)fo->p1 - (double)fo->p0) / (double)fo->fterm);
      } else {
        fo->d0 = 0.0f;
        fo->p0 = fo->p1;
      }
    }
    *out_value = f32_madd_d((double)fo->d0, (double)fo->time, (double)fo->p0);
    return 1u;
  }
  if (fo->op_intrp == HSD_A_OP_SPL0 || fo->op_intrp == HSD_A_OP_SPL ||
      fo->op_intrp == HSD_A_OP_SLP) {
    if (fo->fterm == 0) {
      *out_value = fo->p1;
      return 1u;
    }
    const float inv = f32_from_double(1.0 / (double)fo->fterm);
    *out_value = spl_get_helmite_f32(inv, fo->time, fo->p0, fo->p1, fo->d0, fo->d1);
    return 1u;
  }
  return 0u;
}

static uint8_t fobj_interpret(MslFObjEval* fo, float rate, float* out_value) {
  if (fo->parse_error) {
    return 0u;
  }
  uint8_t any = 0u;
  float last = 0.0f;
  if (fo->state == 0) {
    return 0u;
  }
  fo->time = f32_from_double((double)fo->time + (double)rate);
  if (fo->time < 0.0f) {
    return 0u;
  }
  float fterm = 0.0f;
  // HSD_FObjInterpretAnim keeps helper EOF returns in the interpreter-local state machine. The
  // data/wait EOF helpers return 6 to finish this interpret call, but source does not persist
  // `fo->state = 6` at those EOF branches.
  // refs/melee/src/sysdolphin/baselib/fobj.c::{FObjLoadData,FObjLoadWait,HSD_FObjInterpretAnim}
  // refs/melee/build/GALE01/asm/sysdolphin/baselib/fobj.s
  int state = fo->state;
  for (int iters = 0; iters < 100000; iters++) {
    const int st = state;
    if (st == 6) {
      fo->time = f32_from_double((double)fo->time + (double)fterm);
      fobj_launch_key_data(fo);
      float v = 0.0f;
      if (fobj_update_anim(fo, &v)) {
        last = v;
        any = 1u;
      }
      *out_value = last;
      return any;
    }
    if (st == FOBJ_LOAD_DATA0 || st == FOBJ_LOAD_DATA) {
      state = fobj_load_data(fo);
      if (fo->parse_error) {
        *out_value = last;
        return any;
      }
      continue;
    }
    if (st == FOBJ_LOAD_WAIT) {
      if (fo->flags_80) {
        float v = 0.0f;
        if (fobj_update_anim(fo, &v)) {
          last = v;
          any = 1u;
        }
      }
      state = fobj_load_wait(fo);
      if (fo->parse_error) {
        *out_value = last;
        return any;
      }
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
        any = 1u;
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
  *out_value = last;
  return any;
}

static void free_table(MslAnimPoseTable* t) {
  if (t == NULL) {
    return;
  }
  alloc_free(t->have_msid);
  alloc_free(t->frame_count_by_msid);
  alloc_free(t->base_off_by_msid);
  alloc_free(t->part_to_joint_index);
  alloc_free(t->local_buf);
  alloc_free(t->local_part_to_index);
  alloc_free(t->local_parent_part_by_index);
  alloc_free(t->local_flags_by_index);
  alloc_free(t->local_have_msid);
  alloc_free(t->local_frame_count_by_msid);
  alloc_free(t->local_base_off_by_msid);
  alloc_free(t->dyn_collision_have_msid);
  alloc_free(t->dyn_cone_have_msid);
  alloc_free(t->dyn_catch_grabbable_have_msid);
  alloc_free(t->track_buf);
  alloc_free(t->track_part_to_index);
  alloc_free(t->track_msid_to_anim_index);
  alloc_free(t->track_part_record_off_by_anim_li);
  alloc_free(t->exact_local_base_by_msid);
  alloc_free(t->exact_integer_locals);
  alloc_free(t->buf);
  *t = (MslAnimPoseTable){0};
}

static int load_locals_into_table(const char* data_dir, const char* rel_path, MslAnimPoseTable* t) {
  if (data_dir == NULL || rel_path == NULL || t == NULL) {
    return -1;
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    alloc_free(buf);
    return -1;
  }

  static const uint8_t local_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', 'L', '1'};
  if (sz < ANIM_HDR_BASE_BYTES || memcmp(buf, local_magic, ANIM_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != 1u) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t local_count = read_u16_le(buf + 12);
  const uint16_t anim_count = read_u16_le(buf + 14);
  if (local_count == 0) {
    alloc_free(buf);
    return -1;
  }
  size_t off = ANIM_HDR_BASE_BYTES;
  const uint64_t header_need = (uint64_t)off + (uint64_t)local_count + (uint64_t)local_count * 2u +
                               (uint64_t)local_count * 4u;
  if (header_need > (uint64_t)sz) {
    alloc_free(buf);
    return -1;
  }

  uint16_t* local_part_to_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  int16_t* local_parent_part_by_index =
      (int16_t*)alloc_malloc((size_t)local_count * sizeof(int16_t));
  uint32_t* local_flags_by_index = (uint32_t*)alloc_malloc((size_t)local_count * sizeof(uint32_t));
  uint8_t* local_have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint16_t* local_frame_count_by_msid = (uint16_t*)alloc_calloc(65536, sizeof(uint16_t));
  uint32_t* local_base_off_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  if (local_part_to_index == NULL || local_parent_part_by_index == NULL ||
      local_flags_by_index == NULL || local_have_msid == NULL ||
      local_frame_count_by_msid == NULL || local_base_off_by_msid == NULL) {
    alloc_free(local_part_to_index);
    alloc_free(local_parent_part_by_index);
    alloc_free(local_flags_by_index);
    alloc_free(local_have_msid);
    alloc_free(local_frame_count_by_msid);
    alloc_free(local_base_off_by_msid);
    alloc_free(buf);
    return -1;
  }
  memset(local_part_to_index, 0xFF, 65536 * sizeof(uint16_t));

  for (uint16_t li = 0; li < local_count; li++) {
    const uint8_t part = buf[off + (size_t)li];
    local_part_to_index[(uint16_t)part] = li;
  }
  off += (size_t)local_count;

  for (uint16_t li = 0; li < local_count; li++) {
    uint16_t raw = read_u16_le(buf + off + (size_t)li * 2u);
    local_parent_part_by_index[li] = (int16_t)raw;
  }
  off += (size_t)local_count * 2u;

  for (uint16_t li = 0; li < local_count; li++) {
    local_flags_by_index[li] = read_u32_le(buf + off + (size_t)li * 4u);
  }
  off += (size_t)local_count * 4u;

  enum { LOCAL_SRT_BYTES = 9 * 4 };
  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if (off + 4u > sz) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint16_t frame_count = read_u16_le(buf + off + 2);
    off += 4u;
    if (local_have_msid[msid]) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    local_have_msid[msid] = 1u;
    local_frame_count_by_msid[msid] = frame_count;
    if (off > 0xFFFFFFFFu) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    local_base_off_by_msid[msid] = (uint32_t)off;

    const uint64_t need = (uint64_t)frame_count * (uint64_t)local_count * (uint64_t)LOCAL_SRT_BYTES;
    if (need > (uint64_t)(sz - off)) {
      alloc_free(local_part_to_index);
      alloc_free(local_parent_part_by_index);
      alloc_free(local_flags_by_index);
      alloc_free(local_have_msid);
      alloc_free(local_frame_count_by_msid);
      alloc_free(local_base_off_by_msid);
      alloc_free(buf);
      return -1;
    }
    off += (size_t)need;
  }

  if (off != sz) {
    alloc_free(local_part_to_index);
    alloc_free(local_parent_part_by_index);
    alloc_free(local_flags_by_index);
    alloc_free(local_have_msid);
    alloc_free(local_frame_count_by_msid);
    alloc_free(local_base_off_by_msid);
    alloc_free(buf);
    return -1;
  }

  t->local_buf = buf;
  t->local_sz = sz;
  t->local_count = local_count;
  t->local_anim_count = anim_count;
  t->local_part_to_index = local_part_to_index;
  t->local_parent_part_by_index = local_parent_part_by_index;
  t->local_flags_by_index = local_flags_by_index;
  t->local_have_msid = local_have_msid;
  t->local_frame_count_by_msid = local_frame_count_by_msid;
  t->local_base_off_by_msid = local_base_off_by_msid;
  return 0;
}

static int load_dynamics_into_table(const char* data_dir, const char* rel_path,
                                    MslAnimPoseTable* t) {
  if (data_dir == NULL || rel_path == NULL || t == NULL) {
    return -1;
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    alloc_free(buf);
    return -1;
  }

  static const uint8_t dyn_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'D', 'Y', 'N', 'N', '0', '1'};
  const uint32_t ver = (sz >= ANIM_HDR_BASE_BYTES) ? read_u32_le(buf + 8) : 0u;
  if (sz < ANIM_HDR_BASE_BYTES || memcmp(buf, dyn_magic, ANIM_MAGIC_LEN) != 0 ||
      ver != ANIM_DYN_VERSION_V8) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t set_count = read_u16_le(buf + 12);
  const uint16_t total_nodes = read_u16_le(buf + 14);
  if (set_count > (uint16_t)ANIM_DYN_MAX_SETS || total_nodes > (uint16_t)MSL_MAX_DYNAMIC_NODES) {
    alloc_free(buf);
    return -1;
  }

  size_t off = ANIM_HDR_BASE_BYTES;
  uint16_t seen_nodes = 0;
  memset(t->dyn_sets, 0, sizeof(t->dyn_sets));
  memset(t->dyn_colliders, 0, sizeof(t->dyn_colliders));
  for (uint16_t si = 0; si < set_count; si++) {
    if (off + 16u > sz) {
      alloc_free(buf);
      return -1;
    }
    MslAnimDynSetData* set = &t->dyn_sets[si];
    set->root_part = read_u16_le(buf + off);
    set->node_count = read_u16_le(buf + off + 2u);
    set->pos[0] = read_f32_le(buf + off + 4u);
    set->pos[1] = read_f32_le(buf + off + 8u);
    set->pos[2] = read_f32_le(buf + off + 12u);
    off += 16u;
    if (set->node_count > (uint16_t)MSL_MAX_DYNAMIC_NODES) {
      alloc_free(buf);
      return -1;
    }
    if ((uint32_t)seen_nodes + (uint32_t)set->node_count > (uint32_t)total_nodes) {
      alloc_free(buf);
      return -1;
    }
    for (uint16_t ni = 0; ni < set->node_count; ni++) {
      if (off + 64u > sz) {
        alloc_free(buf);
        return -1;
      }
      set->nodes[ni].part_id = read_u16_le(buf + off);
      off += 4u;  // part + pad
      for (uint16_t ci = 0; ci < 15u; ci++) {
        set->nodes[ni].c[ci] = read_f32_le(buf + off + (size_t)ci * 4u);
      }
      off += 15u * 4u;
    }
    seen_nodes = (uint16_t)(seen_nodes + set->node_count);
  }
  uint8_t* dyn_collision_have_msid = NULL;
  uint8_t* dyn_cone_have_msid = NULL;
  uint8_t* dyn_catch_grabbable_have_msid = NULL;
  if (off + 4u > sz) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t collision_msid_count = read_u16_le(buf + off);
  off += 4u;  // collision_msid_count + reserved
  if (off + (size_t)collision_msid_count * 2u > sz) {
    alloc_free(buf);
    return -1;
  }
  dyn_collision_have_msid = (uint8_t*)alloc_calloc(65536, 1);
  if (dyn_collision_have_msid == NULL) {
    alloc_free(buf);
    return -1;
  }
  for (uint16_t i = 0; i < collision_msid_count; i++) {
    const uint16_t msid = read_u16_le(buf + off + (size_t)i * 2u);
    dyn_collision_have_msid[msid] = 1u;
  }
  off += (size_t)collision_msid_count * 2u;
  dyn_cone_have_msid = (uint8_t*)alloc_calloc(65536, 1);
  if (dyn_cone_have_msid == NULL) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    return -1;
  }
  if (off + 4u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  const uint16_t source_step_msid_count = read_u16_le(buf + off);
  off += 4u;  // source_step_msid_count + reserved
  if (source_step_msid_count != 0u) {
    // SSDYNN01 v8 reserves a source-step owner index, but this stack intentionally hard-disables
    // the runtime mode after the DamageAir2 source-step attempt was rejected. Non-empty artifacts
    // must fail loudly until the full source-order dynamic/AObj owner is implemented.
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  if (off + (size_t)source_step_msid_count * 2u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  off += (size_t)source_step_msid_count * 2u;
  if (off + 4u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  const uint16_t cone_msid_count = read_u16_le(buf + off);
  off += 4u;  // cone_msid_count + reserved
  if (off + (size_t)cone_msid_count * 2u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  for (uint16_t i = 0; i < cone_msid_count; i++) {
    const uint16_t msid = read_u16_le(buf + off + (size_t)i * 2u);
    if (!dyn_collision_have_msid[msid]) {
      alloc_free(buf);
      alloc_free(dyn_collision_have_msid);
      alloc_free(dyn_cone_have_msid);
      return -1;
    }
    dyn_cone_have_msid[msid] = 1u;
  }
  off += (size_t)cone_msid_count * 2u;
  dyn_catch_grabbable_have_msid = (uint8_t*)alloc_calloc(65536, 1);
  if (dyn_catch_grabbable_have_msid == NULL) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    return -1;
  }
  if (off + 4u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    alloc_free(dyn_catch_grabbable_have_msid);
    return -1;
  }
  const uint16_t catch_grabbable_msid_count = read_u16_le(buf + off);
  off += 4u;  // catch_grabbable_msid_count + reserved
  if (off + (size_t)catch_grabbable_msid_count * 2u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    alloc_free(dyn_catch_grabbable_have_msid);
    return -1;
  }
  for (uint16_t i = 0; i < catch_grabbable_msid_count; i++) {
    const uint16_t msid = read_u16_le(buf + off + (size_t)i * 2u);
    dyn_catch_grabbable_have_msid[msid] = 1u;
  }
  off += (size_t)catch_grabbable_msid_count * 2u;
  if (off + 4u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    alloc_free(dyn_catch_grabbable_have_msid);
    return -1;
  }
  const uint16_t collider_count = read_u16_le(buf + off);
  off += 4u;  // collider_count + reserved
  if (collider_count > (uint16_t)ANIM_DYN_MAX_COLLIDERS ||
      off + (size_t)collider_count * 20u > sz) {
    alloc_free(buf);
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    alloc_free(dyn_catch_grabbable_have_msid);
    return -1;
  }
  for (uint16_t ci = 0; ci < collider_count; ci++) {
    MslAnimDynColliderData* col = &t->dyn_colliders[ci];
    col->part_id = read_u16_le(buf + off);
    off += 4u;  // part + pad
    col->offset[0] = read_f32_le(buf + off);
    col->offset[1] = read_f32_le(buf + off + 4u);
    col->offset[2] = read_f32_le(buf + off + 8u);
    col->radius = read_f32_le(buf + off + 12u);
    off += 16u;
  }
  alloc_free(buf);
  if (off != sz || seen_nodes != total_nodes) {
    alloc_free(dyn_collision_have_msid);
    alloc_free(dyn_cone_have_msid);
    alloc_free(dyn_catch_grabbable_have_msid);
    return -1;
  }
  t->dyn_set_count = set_count;
  t->dyn_total_nodes = total_nodes;
  t->dyn_collider_count = collider_count;
  t->dyn_collision_have_msid = dyn_collision_have_msid;
  t->dyn_cone_have_msid = dyn_cone_have_msid;
  t->dyn_catch_grabbable_have_msid = dyn_catch_grabbable_have_msid;
  return 0;
}

static int load_tracks_into_table(const char* data_dir, const char* rel_path, MslAnimPoseTable* t) {
  if (data_dir == NULL || rel_path == NULL || t == NULL) {
    return -1;
  }

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    alloc_free(buf);
    return -1;
  }

  static const uint8_t track_magic[ANIM_MAGIC_LEN] = {'S', 'S', 'A', 'N', 'I', 'M', 'T', '1'};
  if (sz < ANIM_HDR_BASE_BYTES || memcmp(buf, track_magic, ANIM_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != 3u) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t local_count = read_u16_le(buf + 12);
  const uint16_t anim_count = read_u16_le(buf + 14);
  if (local_count == 0u || anim_count == 0u) {
    alloc_free(buf);
    return -1;
  }
  size_t off = ANIM_HDR_BASE_BYTES;
  const uint64_t header_need = (uint64_t)off + (uint64_t)local_count + (uint64_t)local_count * 2u +
                               (uint64_t)local_count * 4u;
  if (header_need > (uint64_t)sz) {
    alloc_free(buf);
    return -1;
  }
  if (t->local_count != 0u && local_count != t->local_count) {
    alloc_free(buf);
    return -1;
  }

  uint16_t* track_part_to_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  uint16_t* track_msid_to_anim_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  uint32_t* record_off_by_anim_li =
      (uint32_t*)alloc_calloc((size_t)anim_count * (size_t)local_count, sizeof(uint32_t));
  if (track_part_to_index == NULL || track_msid_to_anim_index == NULL ||
      record_off_by_anim_li == NULL) {
    alloc_free(track_part_to_index);
    alloc_free(track_msid_to_anim_index);
    alloc_free(record_off_by_anim_li);
    alloc_free(buf);
    return -1;
  }
  memset(track_part_to_index, 0xFF, 65536 * sizeof(uint16_t));
  memset(track_msid_to_anim_index, 0xFF, 65536 * sizeof(uint16_t));

  for (uint16_t li = 0; li < local_count; li++) {
    const uint8_t part = buf[off + (size_t)li];
    track_part_to_index[(uint16_t)part] = li;
    if (t->local_part_to_index != NULL && t->local_part_to_index[(uint16_t)part] != li) {
      alloc_free(track_part_to_index);
      alloc_free(track_msid_to_anim_index);
      alloc_free(record_off_by_anim_li);
      alloc_free(buf);
      return -1;
    }
  }
  off += (size_t)local_count + (size_t)local_count * 2u + (size_t)local_count * 4u;

  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if (off + 8u > sz) {
      alloc_free(track_part_to_index);
      alloc_free(track_msid_to_anim_index);
      alloc_free(record_off_by_anim_li);
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off);
    off += 2u;
    off += 4u;  // end_frame
    off += 1u;  // aobj_loop
    off += 1u;  // uses_root_motion
    if (track_msid_to_anim_index[msid] != 0xFFFFu) {
      alloc_free(track_part_to_index);
      alloc_free(track_msid_to_anim_index);
      alloc_free(record_off_by_anim_li);
      alloc_free(buf);
      return -1;
    }
    track_msid_to_anim_index[msid] = ai;

    for (uint16_t li = 0; li < local_count; li++) {
      if (off + 2u > sz || off > 0xFFFFFFFFu) {
        alloc_free(track_part_to_index);
        alloc_free(track_msid_to_anim_index);
        alloc_free(record_off_by_anim_li);
        alloc_free(buf);
        return -1;
      }
      record_off_by_anim_li[(size_t)ai * (size_t)local_count + (size_t)li] = (uint32_t)off;
      const uint8_t part = buf[off + 0u];
      const uint8_t n_tracks = buf[off + 1u];
      if (track_part_to_index[(uint16_t)part] != li) {
        alloc_free(track_part_to_index);
        alloc_free(track_msid_to_anim_index);
        alloc_free(record_off_by_anim_li);
        alloc_free(buf);
        return -1;
      }
      off += 2u;
      for (uint8_t ti = 0; ti < n_tracks; ti++) {
        if (off + 8u > sz) {
          alloc_free(track_part_to_index);
          alloc_free(track_msid_to_anim_index);
          alloc_free(record_off_by_anim_li);
          alloc_free(buf);
          return -1;
        }
        const uint8_t obj_type = buf[off + 0u];
        const uint8_t frac_value = buf[off + 1u];
        const uint8_t frac_slope = buf[off + 2u];
        const uint16_t startframe = read_u16_le(buf + off + 4u);
        const uint16_t len = read_u16_le(buf + off + 6u);
        off += 8u;
        if (off + (size_t)len > sz) {
          alloc_free(track_part_to_index);
          alloc_free(track_msid_to_anim_index);
          alloc_free(record_off_by_anim_li);
          alloc_free(buf);
          return -1;
        }
        // Runtime init only needs structural offsets here. The extractor/build-data tests own
        // full FObj payload validation; repeating it on every sim process start is pure startup
        // cost for validation/eval and does not change runtime semantics.
        (void)obj_type;
        (void)frac_value;
        (void)frac_slope;
        (void)startframe;
        off += (size_t)len;
      }
    }
  }

  if (off != sz) {
    alloc_free(track_part_to_index);
    alloc_free(track_msid_to_anim_index);
    alloc_free(record_off_by_anim_li);
    alloc_free(buf);
    return -1;
  }

  t->track_buf = buf;
  t->track_sz = sz;
  t->track_local_count = local_count;
  t->track_anim_count = anim_count;
  t->track_part_to_index = track_part_to_index;
  t->track_msid_to_anim_index = track_msid_to_anim_index;
  t->track_part_record_off_by_anim_li = record_off_by_anim_li;
  return 0;
}

static int load_pose_for_char(const char* data_dir, const char* rel_path, uint8_t char_id) {
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc_uninit(sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    alloc_free(buf);
    return -1;
  }

  if (sz < ANIM_HDR_BASE_BYTES) {
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_anim_magic, ANIM_MAGIC_LEN) != 0) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != (uint32_t)ANIM_VERSION_V5) {
    alloc_free(buf);
    return -1;
  }
  const uint16_t joint_count = read_u16_le(buf + 12);
  const uint16_t anim_count = read_u16_le(buf + 14);
  if (joint_count == 0) {
    alloc_free(buf);
    return -1;
  }
  if (sz < ANIM_HDR_BASE_BYTES + (size_t)joint_count) {
    alloc_free(buf);
    return -1;
  }

  uint8_t* have_msid = (uint8_t*)alloc_calloc(65536, 1);
  uint16_t* frame_count_by_msid = (uint16_t*)alloc_calloc(65536, sizeof(uint16_t));
  uint32_t* base_off_by_msid = (uint32_t*)alloc_calloc(65536, sizeof(uint32_t));
  uint16_t* part_to_joint_index = (uint16_t*)alloc_malloc(65536 * sizeof(uint16_t));
  if (have_msid == NULL || frame_count_by_msid == NULL || base_off_by_msid == NULL ||
      part_to_joint_index == NULL) {
    alloc_free(have_msid);
    alloc_free(frame_count_by_msid);
    alloc_free(base_off_by_msid);
    alloc_free(part_to_joint_index);
    alloc_free(buf);
    return -1;
  }
  memset(part_to_joint_index, 0xFF, 65536 * sizeof(uint16_t));  // 0xFFFF sentinel

  // Build part_id -> joint index mapping from joint_parts bytes in the header (like the extractors).
  for (uint16_t ji = 0; ji < joint_count; ji++) {
    const uint8_t part_id = buf[ANIM_HDR_BASE_BYTES + (size_t)ji];
    part_to_joint_index[(uint16_t)part_id] = ji;  // overwrite on duplicates (Python dict behavior)
  }

  // Walk payload (tools/extraction/extract_ecb_extents.py::_extract_ecb_extents_for_anim_file).
  size_t off = ANIM_HDR_BASE_BYTES + (size_t)joint_count;
  const uint64_t joint_count_u = (uint64_t)joint_count;
  for (uint16_t ai = 0; ai < anim_count; ai++) {
    if (off + 4u > sz) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    const uint16_t msid = read_u16_le(buf + off + 0);
    const uint16_t frame_count = read_u16_le(buf + off + 2);
    off += 4u;

    if (have_msid[msid]) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }

    // base_offset points to first frame's first joint matrix record (payload matrices start).
    have_msid[msid] = 1;
    frame_count_by_msid[msid] = frame_count;
    if (off > 0xFFFFFFFFu) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    base_off_by_msid[msid] = (uint32_t)off;

    const uint64_t frame_count_u = (uint64_t)frame_count;
    const uint64_t mats_bytes = frame_count_u * joint_count_u * (uint64_t)MAT_BYTES;
    const uint64_t transn_bytes = frame_count_u * (uint64_t)TRANSN_BYTES_PER_FRAME;
    const uint64_t need_u = mats_bytes + transn_bytes;
    if (need_u > (uint64_t)(sz - off)) {
      alloc_free(have_msid);
      alloc_free(frame_count_by_msid);
      alloc_free(base_off_by_msid);
      alloc_free(part_to_joint_index);
      alloc_free(buf);
      return -1;
    }
    off += (size_t)need_u;
  }

  // Fail loudly on unexpected extension bytes (extractors enforce this today).
  if (off != sz) {
    alloc_free(have_msid);
    alloc_free(frame_count_by_msid);
    alloc_free(base_off_by_msid);
    alloc_free(part_to_joint_index);
    alloc_free(buf);
    return -1;
  }

  MslAnimPoseTable next = {
      .buf = buf,
      .sz = sz,
      .joint_count = joint_count,
      .anim_count = anim_count,
      .have_msid = have_msid,
      .frame_count_by_msid = frame_count_by_msid,
      .base_off_by_msid = base_off_by_msid,
      .part_to_joint_index = part_to_joint_index,
      .have = 1,
  };
  const char* slash = strrchr(rel_path, '/');
  const char* file = (slash != NULL) ? slash + 1 : rel_path;
  const char* dot = strchr(file, '.');
  const int stem_len = (dot != NULL) ? (int)(dot - file) : (int)strlen(file);
  char locals_rel[128];
  int local_status = 1;
  if (snprintf(locals_rel, sizeof(locals_rel), "anims/%.*s.locals.bin", stem_len, file) > 0) {
    // Local SRT tables are required whenever dynamic BODY pose data is present. Older synthetic
    // tests that provide only SSANIM01 still run through anim_pose_get_matrix().
    local_status = load_locals_into_table(data_dir, locals_rel, &next);
    if (local_status < 0) {
      fprintf(stderr, "msl: anim locals load failed: %s/%s\n", data_dir, locals_rel);
      free_table(&next);
      return -1;
    }
  }
  char dyn_rel[128];
  int dyn_status = 1;
  if (snprintf(dyn_rel, sizeof(dyn_rel), "anims/%.*s.dyn.bin", stem_len, file) > 0) {
    dyn_status = load_dynamics_into_table(data_dir, dyn_rel, &next);
    if (dyn_status < 0) {
      fprintf(stderr, "msl: anim dynamics load failed: %s/%s\n", data_dir, dyn_rel);
      free_table(&next);
      return -1;
    }
    if (dyn_status == 0 && local_status != 0) {
      fprintf(stderr, "msl: anim dynamics present without locals: %s/%s\n", data_dir, dyn_rel);
      free_table(&next);
      return -1;
    }
  }
  char tracks_rel[128];
  if (snprintf(tracks_rel, sizeof(tracks_rel), "anims/%.*s.tracks.bin", stem_len, file) > 0) {
    // SSANIMT1 is the extracted HSD AObj/FObj stream. It is optional for synthetic pose-only
    // tests, but present files are a C-core data contract and must parse cleanly.
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    const int track_status = load_tracks_into_table(data_dir, tracks_rel, &next);
    if (track_status < 0) {
      fprintf(stderr, "msl: anim tracks load failed: %s/%s\n", data_dir, tracks_rel);
      free_table(&next);
      return -1;
    }
    if (track_status == 0 && build_exact_integer_local_cache(&next) != 0) {
      fprintf(stderr, "msl: anim exact local cache build failed: %s/%s\n", data_dir, tracks_rel);
      free_table(&next);
      return -1;
    }
  }

  // Replace any existing table for this character.
  if (g_table_by_char[char_id].buf) {
    free_table(&g_table_by_char[char_id]);
  }
  g_table_by_char[char_id] = next;
  return 0;
}

int anim_pose_init(void) {
  if (g_loaded) {
    return 0;
  }

  const char* data_dir = msl_data_dir();

  for (int ci = 0; ci < MSL_CHAR_REGISTRY_COUNT; ci++) {
    char rel[64];
    snprintf(rel, sizeof(rel), "anims/%s.bin", MSL_CHAR_REGISTRY[ci].name);
    if (load_pose_for_char(data_dir, rel, MSL_CHAR_REGISTRY[ci].char_id) != 0) {
      for (int cj = 0; cj < ci; cj++) {
        free_table(&g_table_by_char[MSL_CHAR_REGISTRY[cj].char_id]);
      }
      return -1;
    }
  }

  g_loaded = 1;
  return 0;
}

static const MslAnimPoseTable* table_for_char(uint8_t char_id) {
  if (!g_loaded) {
    return NULL;
  }
  const MslAnimPoseTable* t = &g_table_by_char[char_id];
  if (!t->have || t->buf == NULL || t->have_msid == NULL || t->frame_count_by_msid == NULL ||
      t->base_off_by_msid == NULL || t->part_to_joint_index == NULL || t->joint_count == 0) {
    return NULL;
  }
  return t;
}

void anim_pose_reset_for_tests(void) {
  for (int i = 0; i < 256; i++) {
    free_table(&g_table_by_char[i]);
  }
  g_loaded = 0;
}

int anim_pose_get_matrix(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t part_id,
                         float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  if (!t->have_msid[msid]) {
    return -1;
  }

  const uint16_t frame_count = t->frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }

  const uint16_t joint_index = t->part_to_joint_index[part_id];
  if (joint_index == 0xFFFFu || joint_index >= t->joint_count) {
    return -1;
  }

  const uint32_t base_off = t->base_off_by_msid[msid];
  const uint64_t joint_count_u = (uint64_t)t->joint_count;
  const uint64_t frame_u = (uint64_t)frame;
  const uint64_t joint_u = (uint64_t)joint_index;

  const uint64_t mat_off_u = (uint64_t)base_off + frame_u * joint_count_u * (uint64_t)MAT_BYTES +
                             joint_u * (uint64_t)MAT_BYTES;
  if (mat_off_u + (uint64_t)MAT_BYTES > (uint64_t)t->sz) {
    return -1;
  }

  memcpy(out_3x4, t->buf + (size_t)mat_off_u, (size_t)MAT_BYTES);
  return 0;
}

static int local_srt_for_part(const MslAnimPoseTable* t, uint16_t msid, uint16_t frame,
                              uint16_t part_id, float rot[3], float pos[3], float scl[3],
                              uint32_t* out_flags, int16_t* out_parent) {
  if (t == NULL || rot == NULL || pos == NULL || scl == NULL || t->local_buf == NULL ||
      t->local_have_msid == NULL || t->local_part_to_index == NULL ||
      t->local_frame_count_by_msid == NULL || t->local_base_off_by_msid == NULL ||
      t->local_flags_by_index == NULL || t->local_parent_part_by_index == NULL) {
    return -1;
  }
  if (!t->local_have_msid[msid]) {
    return -1;
  }
  const uint16_t frame_count = t->local_frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }
  const uint16_t li = t->local_part_to_index[part_id];
  if (li == 0xFFFFu || li >= t->local_count) {
    return -1;
  }
  enum { LOCAL_SRT_FLOATS = 9, LOCAL_SRT_BYTES = LOCAL_SRT_FLOATS * 4 };
  const uint64_t off_u = (uint64_t)t->local_base_off_by_msid[msid] +
                         (uint64_t)frame * (uint64_t)t->local_count * (uint64_t)LOCAL_SRT_BYTES +
                         (uint64_t)li * (uint64_t)LOCAL_SRT_BYTES;
  if (off_u + (uint64_t)LOCAL_SRT_BYTES > (uint64_t)t->local_sz) {
    return -1;
  }
  const uint8_t* p = t->local_buf + (size_t)off_u;
  memcpy(rot, p + 0, 3u * sizeof(float));
  memcpy(pos, p + 12, 3u * sizeof(float));
  memcpy(scl, p + 24, 3u * sizeof(float));
  if (out_flags != NULL) {
    *out_flags = t->local_flags_by_index[li];
  }
  if (out_parent != NULL) {
    *out_parent = t->local_parent_part_by_index[li];
  }
  return 0;
}

int anim_pose_get_local_translation(uint8_t char_id, uint16_t msid, uint16_t frame,
                                    uint16_t part_id, float out_xyz[3]) {
  if (out_xyz == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  float rot[3], pos[3], scl[3];
  if (local_srt_for_part(t, msid, frame, part_id, rot, pos, scl, NULL, NULL) != 0) {
    return -1;
  }
  (void)rot;
  (void)scl;
  out_xyz[0] = pos[0];
  out_xyz[1] = pos[1];
  out_xyz[2] = pos[2];
  return 0;
}

static int local_srt_for_part_f32_uncached(const MslAnimPoseTable* t, uint16_t msid,
                                           float anim_frame, uint16_t part_id, float rot[3],
                                           float pos[3], float scl[3], uint32_t* out_flags,
                                           int16_t* out_parent) {
  if (t == NULL || rot == NULL || pos == NULL || scl == NULL) {
    return -1;
  }
  const uint16_t frame0 = 0u;
  if (local_srt_for_part(t, msid, frame0, part_id, rot, pos, scl, out_flags, out_parent) != 0) {
    return -1;
  }
  if (t->track_buf == NULL || t->track_msid_to_anim_index == NULL ||
      t->track_part_to_index == NULL || t->track_part_record_off_by_anim_li == NULL ||
      !isfinite(anim_frame)) {
    return 0;
  }

  const uint16_t ai = t->track_msid_to_anim_index[msid];
  const uint16_t li = t->track_part_to_index[part_id];
  if (ai == 0xFFFFu || ai >= t->track_anim_count || li == 0xFFFFu || li >= t->track_local_count) {
    return 0;
  }

  const uint32_t rec_off =
      t->track_part_record_off_by_anim_li[(size_t)ai * (size_t)t->track_local_count + (size_t)li];
  if ((uint64_t)rec_off + 2u > (uint64_t)t->track_sz) {
    return -1;
  }
  const uint8_t part = t->track_buf[(size_t)rec_off + 0u];
  const uint8_t n_tracks = t->track_buf[(size_t)rec_off + 1u];
  if ((uint16_t)part != part_id) {
    return -1;
  }
  size_t off = (size_t)rec_off + 2u;
  for (uint8_t ti = 0; ti < n_tracks; ti++) {
    if (off + 8u > t->track_sz) {
      return -1;
    }
    const uint8_t obj_type = t->track_buf[off + 0u];
    const uint8_t frac_value = t->track_buf[off + 1u];
    const uint8_t frac_slope = t->track_buf[off + 2u];
    const uint16_t startframe = read_u16_le(t->track_buf + off + 4u);
    const uint16_t length = read_u16_le(t->track_buf + off + 6u);
    off += 8u;
    if (off + (size_t)length > t->track_sz) {
      return -1;
    }
    if (obj_type >= 1u && obj_type <= 10u) {
      MslFObjEval fo = {
          .ad = t->track_buf + off,
          .length = (int)length,
          .startframe = (int)startframe,
          .obj_type = obj_type,
          .frac_value = frac_value,
          .frac_slope = frac_slope,
      };
      fobj_req_anim(&fo, anim_frame);
      float v = 0.0f;
      if (fobj_interpret(&fo, 0.0f, &v)) {
        if (obj_type == 1u) {
          rot[0] = v;
        } else if (obj_type == 2u) {
          rot[1] = v;
        } else if (obj_type == 3u) {
          rot[2] = v;
        } else if (obj_type == 5u) {
          pos[0] = v;
        } else if (obj_type == 6u) {
          pos[1] = v;
        } else if (obj_type == 7u) {
          pos[2] = v;
        } else if (obj_type == 8u) {
          const float av = fabsf(v);
          scl[0] = (av < 1.0e-3f) ? 1.0e-3f : av;
        } else if (obj_type == 9u) {
          const float av = fabsf(v);
          scl[1] = (av < 1.0e-3f) ? 1.0e-3f : av;
        } else if (obj_type == 10u) {
          const float av = fabsf(v);
          scl[2] = (av < 1.0e-3f) ? 1.0e-3f : av;
        }
      }
      if (fo.parse_error) {
        return -1;
      }
    }
    off += (size_t)length;
  }
  return 0;
}

static int build_exact_integer_local_cache(MslAnimPoseTable* t) {
  if (t == NULL || t->track_buf == NULL || t->track_local_count == 0u ||
      t->local_frame_count_by_msid == NULL) {
    return 0;
  }
  enum { FIRST_MSID = MSL_SM_FALL, LAST_MSID = MSL_SM_FALL_SPECIAL_B, SRT_FLOATS = 9 };
  uint64_t total_floats = 0u;
  for (uint16_t msid = (uint16_t)FIRST_MSID; msid <= (uint16_t)LAST_MSID; msid++) {
    total_floats += (uint64_t)t->local_frame_count_by_msid[msid] * (uint64_t)t->track_local_count *
                    (uint64_t)SRT_FLOATS;
  }
  if (total_floats == 0u || total_floats > (uint64_t)UINT32_MAX) {
    return 0;
  }

  uint32_t* base_by_msid = (uint32_t*)alloc_malloc(65536u * sizeof(uint32_t));
  float* locals = (float*)alloc_malloc_uninit((size_t)total_floats * sizeof(float));
  if (base_by_msid == NULL || locals == NULL) {
    alloc_free(base_by_msid);
    alloc_free(locals);
    return -1;
  }
  memset(base_by_msid, 0xFF, 65536u * sizeof(uint32_t));

  uint32_t base = 0u;
  for (uint16_t msid = (uint16_t)FIRST_MSID; msid <= (uint16_t)LAST_MSID; msid++) {
    const uint16_t frame_count = t->local_frame_count_by_msid[msid];
    if (frame_count == 0u) {
      continue;
    }
    base_by_msid[msid] = base;
    uint8_t complete = 1u;
    for (uint16_t frame = 0u; frame < frame_count && complete; frame++) {
      for (uint16_t li = 0u; li < t->track_local_count; li++) {
        const uint16_t part_id = (uint16_t)t->track_buf[ANIM_HDR_BASE_BYTES + (size_t)li];
        float* out =
            &locals[(size_t)base + ((size_t)frame * (size_t)t->track_local_count + (size_t)li) *
                                       (size_t)SRT_FLOATS];
        if (local_srt_for_part_f32_uncached(t, msid, (float)frame, part_id, out, out + 3, out + 6,
                                            NULL, NULL) != 0) {
          complete = 0u;
          break;
        }
      }
    }
    if (!complete) {
      base_by_msid[msid] = UINT32_MAX;
    }
    base +=
        (uint32_t)((uint32_t)frame_count * (uint32_t)t->track_local_count * (uint32_t)SRT_FLOATS);
  }
  t->exact_local_base_by_msid = base_by_msid;
  t->exact_integer_locals = locals;
  return 0;
}

static int local_srt_for_part_f32(const MslAnimPoseTable* t, uint16_t msid, float anim_frame,
                                  uint16_t part_id, float rot[3], float pos[3], float scl[3],
                                  uint32_t* out_flags, int16_t* out_parent) {
  if (t != NULL && t->exact_local_base_by_msid != NULL && t->exact_integer_locals != NULL &&
      isfinite(anim_frame)) {
    const uint16_t frame = msl_anim_frame_floor_u16(anim_frame);
    const uint32_t base = t->exact_local_base_by_msid[msid];
    const uint16_t li = t->local_part_to_index[part_id];
    if (anim_frame == (float)frame && base != UINT32_MAX &&
        frame < t->local_frame_count_by_msid[msid] && li != 0xFFFFu && li < t->track_local_count) {
      enum { SRT_FLOATS = 9 };
      const float* in =
          &t->exact_integer_locals[(size_t)base +
                                   ((size_t)frame * (size_t)t->track_local_count + (size_t)li) *
                                       (size_t)SRT_FLOATS];
      memcpy(rot, in, 3u * sizeof(float));
      memcpy(pos, in + 3, 3u * sizeof(float));
      memcpy(scl, in + 6, 3u * sizeof(float));
      if (out_flags != NULL) {
        *out_flags = t->local_flags_by_index[li];
      }
      if (out_parent != NULL) {
        *out_parent = t->local_parent_part_by_index[li];
      }
      return 0;
    }
  }
  return local_srt_for_part_f32_uncached(t, msid, anim_frame, part_id, rot, pos, scl, out_flags,
                                         out_parent);
}

static void vec3_cross(const float a[3], const float b[3], float out[3]);
static uint8_t vec3_normalize(float v[3]);
static float vec3_angle(const float a[3], const float b[3]);
static void vec3_rotate_about_unit_axis(const float v[3], const float axis[3], float angle,
                                        float out[3]);

static void mtx34_identity(float m[12]) {
  m[0] = 1.0f;
  m[1] = 0.0f;
  m[2] = 0.0f;
  m[3] = 0.0f;
  m[4] = 0.0f;
  m[5] = 1.0f;
  m[6] = 0.0f;
  m[7] = 0.0f;
  m[8] = 0.0f;
  m[9] = 0.0f;
  m[10] = 1.0f;
  m[11] = 0.0f;
}

static void mtx34_concat(const float a[12], const float b[12], float out[12]) {
  float o[12];
  o[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8];
  o[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9];
  o[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10];
  o[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3];
  o[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8];
  o[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9];
  o[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10];
  o[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7];
  o[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8];
  o[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9];
  o[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10];
  o[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11];
  memcpy(out, o, sizeof(o));
}

static void mtx34_srt_simple(const float rot[3], const float pos[3], const float scl[3],
                             const float* parent_scl, float out[12]) {
  float sx = scl[0], sy = scl[1], sz = scl[2];
  float sx2 = sx, sy2 = sy, sz2 = sz;
  float sx1 = sx, sy1 = sy, sz1 = sz;
  if (parent_scl != NULL) {
    const float psx = parent_scl[0], psy = parent_scl[1], psz = parent_scl[2];
    if (psx != 0.0f && psy != 0.0f && psz != 0.0f) {
      sy2 = sy2 * (psy / psx);
      sz2 = sz2 * (psz / psx);
      sx1 = sx1 * (psx / psy);
      sz1 = sz1 * (psz / psy);
      sx = sx * (psx / psz);
      sy = sy * (psy / psz);
    }
  }

  const float rx = rot[0], ry = rot[1], rz = rot[2];
  const float sin_x = sinf(rx), cos_x = cosf(rx);
  const float sin_y = sinf(ry), cos_y = cosf(ry);
  const float sin_z = sinf(rz), cos_z = cosf(rz);

  out[0] = cos_z * (sx2 * cos_y);
  out[4] = sin_z * (sx1 * cos_y);
  out[8] = -sx * sin_y;

  const float sinx_siny = sin_x * sin_y;
  out[1] = sy2 * (cos_z * sinx_siny - cos_x * sin_z);
  out[5] = sy1 * (sin_z * sinx_siny + cos_x * cos_z);
  out[9] = cos_y * (sy * sin_x);

  const float cosx_siny = cos_x * sin_y;
  out[2] = sz2 * (cos_z * cosx_siny + sin_x * sin_z);
  out[6] = sz1 * (sin_z * cosx_siny - sin_x * cos_z);
  out[10] = cos_y * (sz * cos_x);

  out[3] = pos[0];
  out[7] = pos[1];
  out[11] = pos[2];
}

static void quat_from_unit_mtx34(const float m[12], MslQuat* out) {
  const float trace = m[0] + m[5] + m[10];
  if (trace > 0.0f) {
    const float s = sqrtf(trace + 1.0f) * 2.0f;
    out->w = 0.25f * s;
    out->x = (m[9] - m[6]) / s;
    out->y = (m[2] - m[8]) / s;
    out->z = (m[4] - m[1]) / s;
  } else if (m[0] > m[5] && m[0] > m[10]) {
    const float s = sqrtf(1.0f + m[0] - m[5] - m[10]) * 2.0f;
    out->w = (m[9] - m[6]) / s;
    out->x = 0.25f * s;
    out->y = (m[1] + m[4]) / s;
    out->z = (m[2] + m[8]) / s;
  } else if (m[5] > m[10]) {
    const float s = sqrtf(1.0f + m[5] - m[0] - m[10]) * 2.0f;
    out->w = (m[2] - m[8]) / s;
    out->x = (m[1] + m[4]) / s;
    out->y = 0.25f * s;
    out->z = (m[6] + m[9]) / s;
  } else {
    const float s = sqrtf(1.0f + m[10] - m[0] - m[5]) * 2.0f;
    out->w = (m[4] - m[1]) / s;
    out->x = (m[2] + m[8]) / s;
    out->y = (m[6] + m[9]) / s;
    out->z = 0.25f * s;
  }
}

static void quat_from_euler_srt_order(const float rot[3], MslQuat* out) {
  const float pos[3] = {0.0f, 0.0f, 0.0f};
  const float scl[3] = {1.0f, 1.0f, 1.0f};
  float m[12];
  mtx34_srt_simple(rot, pos, scl, NULL, m);
  quat_from_unit_mtx34(m, out);
}

static void quat_normalize(MslQuat* q) {
  const float len2 = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
  if (!(len2 > 0.0f)) {
    q->x = 0.0f;
    q->y = 0.0f;
    q->z = 0.0f;
    q->w = 1.0f;
    return;
  }
  const float inv = 1.0f / sqrtf(len2);
  q->x *= inv;
  q->y *= inv;
  q->z *= inv;
  q->w *= inv;
}

static void quat_slerp_lb_c490(const MslQuat* target, const MslQuat* neutral, float neutral_t,
                               MslQuat* out) {
  MslQuat q2 = *neutral;
  float dot = target->x * q2.x + target->y * q2.y + target->z * q2.z + target->w * q2.w;
  if (dot < 0.0f) {
    q2.x = -q2.x;
    q2.y = -q2.y;
    q2.z = -q2.z;
    q2.w = -q2.w;
    dot = -dot;
  }
  if (dot > 0.9995f) {
    const float target_t = 1.0f - neutral_t;
    out->x = target->x * target_t + q2.x * neutral_t;
    out->y = target->y * target_t + q2.y * neutral_t;
    out->z = target->z * target_t + q2.z * neutral_t;
    out->w = target->w * target_t + q2.w * neutral_t;
    quat_normalize(out);
    return;
  }
  const float theta0 = acosf(dot);
  const float sin_theta0 = sinf(theta0);
  const float target_t = 1.0f - neutral_t;
  const float s0 = sinf(target_t * theta0) / sin_theta0;
  const float s1 = sinf(neutral_t * theta0) / sin_theta0;
  out->x = target->x * s0 + q2.x * s1;
  out->y = target->y * s0 + q2.y * s1;
  out->z = target->z * s0 + q2.z * s1;
  out->w = target->w * s0 + q2.w * s1;
}

static void mtx34_quat_srt_simple(const MslQuat* q, const float pos[3], const float scl[3],
                                  const float* parent_scl, float out[12]) {
  float sx = scl[0], sy = scl[1], sz = scl[2];
  float sx2 = sx, sy2 = sy, sz2 = sz;
  float sx1 = sx, sy1 = sy, sz1 = sz;
  if (parent_scl != NULL) {
    const float psx = parent_scl[0], psy = parent_scl[1], psz = parent_scl[2];
    if (psx != 0.0f && psy != 0.0f && psz != 0.0f) {
      sy2 = sy2 * (psy / psx);
      sz2 = sz2 * (psz / psx);
      sx1 = sx1 * (psx / psy);
      sz1 = sz1 * (psz / psy);
      sx = sx * (psx / psz);
      sy = sy * (psy / psz);
    }
  }

  const float xx = q->x * q->x;
  const float yy = q->y * q->y;
  const float zz = q->z * q->z;
  const float xy = q->x * q->y;
  const float xz = q->x * q->z;
  const float yz = q->y * q->z;
  const float wx = q->w * q->x;
  const float wy = q->w * q->y;
  const float wz = q->w * q->z;

  const float r00 = 1.0f - 2.0f * (yy + zz);
  const float r01 = 2.0f * (xy - wz);
  const float r02 = 2.0f * (xz + wy);
  const float r10 = 2.0f * (xy + wz);
  const float r11 = 1.0f - 2.0f * (xx + zz);
  const float r12 = 2.0f * (yz - wx);
  const float r20 = 2.0f * (xz - wy);
  const float r21 = 2.0f * (yz + wx);
  const float r22 = 1.0f - 2.0f * (xx + yy);

  out[0] = r00 * sx2;
  out[4] = r10 * sx1;
  out[8] = r20 * sx;
  out[1] = r01 * sy2;
  out[5] = r11 * sy1;
  out[9] = r21 * sy;
  out[2] = r02 * sz2;
  out[6] = r12 * sz1;
  out[10] = r22 * sz;
  out[3] = pos[0];
  out[7] = pos[1];
  out[11] = pos[2];
}

static void mtx34_apply_world_axis_angle(float m[12], const float axis[3], float angle) {
  float col0[3] = {m[0], m[4], m[8]};
  float col1[3] = {m[1], m[5], m[9]};
  float col2[3] = {m[2], m[6], m[10]};
  float out[3];

  vec3_rotate_about_unit_axis(col0, axis, angle, out);
  m[0] = out[0];
  m[4] = out[1];
  m[8] = out[2];
  vec3_rotate_about_unit_axis(col1, axis, angle, out);
  m[1] = out[0];
  m[5] = out[1];
  m[9] = out[2];
  vec3_rotate_about_unit_axis(col2, axis, angle, out);
  m[2] = out[0];
  m[6] = out[1];
  m[10] = out[2];
}

static int dynamic_set_node_index_for_part(const MslAnimDynSetData* set, uint16_t part_id) {
  if (set == NULL) {
    return -1;
  }
  for (uint16_t i = 0; i < set->node_count; i++) {
    if (set->nodes[i].part_id == part_id) {
      return (int)i;
    }
  }
  return -1;
}

static const MslAnimDynSetData* dynamic_set_for_part(const MslAnimPoseTable* t, uint16_t part_id) {
  if (t == NULL) {
    return NULL;
  }
  for (uint16_t si = 0; si < t->dyn_set_count; si++) {
    const MslAnimDynSetData* set = &t->dyn_sets[si];
    if (dynamic_set_node_index_for_part(set, part_id) >= 0) {
      return set;
    }
  }
  return NULL;
}

static size_t dynamic_state_index(size_t player_idx, uint16_t node_i) {
  return player_idx * (size_t)MSL_MAX_DYNAMIC_NODES + (size_t)node_i;
}

static float vec3_dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float vec3_len(const float v[3]) { return sqrtf(vec3_dot(v, v)); }

static uint8_t vec3_normalize(float v[3]) {
  const float len = vec3_len(v);
  if (!(len > 1.0e-6f)) {
    return 0u;
  }
  const float inv = 1.0f / len;
  v[0] *= inv;
  v[1] *= inv;
  v[2] *= inv;
  return 1u;
}

static float vec3_angle(const float a[3], const float b[3]) {
  float d = vec3_dot(a, b);
  if (d < -1.0f) {
    d = -1.0f;
  } else if (d > 1.0f) {
    d = 1.0f;
  }
  return acosf(d);
}

static void vec3_rotate_about_unit_axis(const float v[3], const float axis[3], float angle,
                                        float out[3]) {
  const float s = sinf(angle);
  const float c = cosf(angle);
  const float one_c = 1.0f - c;
  const float dot = vec3_dot(axis, v);
  float cross[3];
  vec3_cross(axis, v, cross);
  out[0] = v[0] * c + cross[0] * s + axis[0] * dot * one_c;
  out[1] = v[1] * c + cross[1] * s + axis[1] * dot * one_c;
  out[2] = v[2] * c + cross[2] * s + axis[2] * dot * one_c;
}

static void vec3_rotate_towards(float v[3], const float target[3], float step_angle) {
  const float angle = vec3_angle(v, target);
  if (!(angle > 1.0e-6f) || !(step_angle > 0.0f)) {
    return;
  }
  float axis[3];
  vec3_cross(v, target, axis);
  if (!vec3_normalize(axis)) {
    return;
  }
  const float step = (step_angle < angle) ? step_angle : angle;
  float out[3];
  vec3_rotate_about_unit_axis(v, axis, step, out);
  if (vec3_normalize(out)) {
    v[0] = out[0];
    v[1] = out[1];
    v[2] = out[2];
  }
}

static uint8_t segment_sphere_intersects(const float a[3], const float b[3], const float c[3],
                                         float tolerance, float radius) {
  float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  float ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  const float ab_len_sq = vec3_dot(ab, ab);
  float t = 0.0f;
  if (ab_len_sq > 1.0e-6f) {
    t = vec3_dot(ac, ab) / ab_len_sq;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float closest[3] = {a[0] + ab[0] * t, a[1] + ab[1] * t, a[2] + ab[2] * t};
  const float dx = c[0] - closest[0];
  const float dy = c[1] - closest[1];
  const float dz = c[2] - closest[2];
  const float r = radius + tolerance;
  return (dx * dx + dy * dy + dz * dz <= r * r) ? 1u : 0u;
}

static uint16_t dynamic_world_colliders(const MslAnimPoseTable* t, uint8_t char_id, uint16_t msid,
                                        uint16_t frame, float out[ANIM_DYN_MAX_COLLIDERS][4]) {
  if (t == NULL || out == NULL || t->dyn_collider_count == 0u) {
    return 0u;
  }
  uint16_t count = 0u;
  for (uint16_t ci = 0; ci < t->dyn_collider_count && ci < (uint16_t)ANIM_DYN_MAX_COLLIDERS; ci++) {
    const MslAnimDynColliderData* col = &t->dyn_colliders[ci];
    float m[12];
    if (anim_pose_get_matrix(char_id, msid, frame, col->part_id, m) != 0) {
      continue;
    }
    const float x = col->offset[0];
    const float y = col->offset[1];
    const float z = col->offset[2];
    out[count][0] = m[0] * x + m[1] * y + m[2] * z + m[3];
    out[count][1] = m[4] * x + m[5] * y + m[6] * z + m[7];
    out[count][2] = m[8] * x + m[9] * y + m[10] * z + m[11];
    out[count][3] = col->radius;
    count++;
  }
  return count;
}

static int dynamic_node_base_positions(const MslAnimDynSetData* set, uint8_t char_id, uint16_t msid,
                                       uint16_t frame, float out_pos[MSL_MAX_DYNAMIC_NODES][3]) {
  if (set == NULL || out_pos == NULL) {
    return -1;
  }
  for (uint16_t ni = 0; ni < set->node_count; ni++) {
    float m[12];
    if (anim_pose_get_matrix(char_id, msid, frame, set->nodes[ni].part_id, m) != 0) {
      return -1;
    }
    out_pos[ni][0] = m[3];
    out_pos[ni][1] = m[7];
    out_pos[ni][2] = m[11];
  }
  return 0;
}

static void dynamic_state_initialize_from_locals(MslBatch* batch, size_t idx,
                                                 const MslAnimPoseTable* t,
                                                 const MslAnimDynSetData* set, uint8_t char_id,
                                                 uint16_t msid, uint16_t frame) {
  if (batch == NULL || t == NULL || set == NULL) {
    return;
  }
  const uint16_t node_count = set->node_count;
  batch->state.dynamic_pose_state_valid[idx] = (node_count != 0u) ? 1u : 0u;
  batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
  batch->state.dynamic_pose_node_count[idx] = (uint8_t)node_count;
  batch->state.dynamic_pose_char_id[idx] = char_id;
  batch->state.dynamic_pose_msid[idx] = msid;
  batch->state.dynamic_pose_frame[idx] = frame;
  for (uint16_t ni = 0; ni < (uint16_t)MSL_MAX_DYNAMIC_NODES; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    batch->state.dynamic_pose_axis_x[di] = 1.0f;
    batch->state.dynamic_pose_axis_y[di] = 0.0f;
    batch->state.dynamic_pose_axis_z[di] = 0.0f;
    batch->state.dynamic_pose_angle[di] = 0.0f;
    if (ni >= node_count) {
      batch->state.dynamic_pose_rot_x[di] = 0.0f;
      batch->state.dynamic_pose_rot_y[di] = 0.0f;
      batch->state.dynamic_pose_rot_z[di] = 0.0f;
      batch->state.dynamic_pose_pos_x[di] = 0.0f;
      batch->state.dynamic_pose_pos_y[di] = 0.0f;
      batch->state.dynamic_pose_pos_z[di] = 0.0f;
      continue;
    }
    float rot[3], pos[3], scl[3];
    if (local_srt_for_part(t, msid, frame, set->nodes[ni].part_id, rot, pos, scl, NULL, NULL) !=
        0) {
      rot[0] = rot[1] = rot[2] = 0.0f;
    }
    const float max_bend = fabsf(set->nodes[ni].c[6]);
    float init_x = rot[0];
    if (ni + 1u < node_count) {
      if (init_x < -max_bend) {
        init_x = -max_bend;
      } else if (init_x > max_bend) {
        init_x = max_bend;
      }
    }
    batch->state.dynamic_pose_rot_x[di] = init_x;
    batch->state.dynamic_pose_rot_y[di] = rot[1];
    batch->state.dynamic_pose_rot_z[di] = rot[2];
    float m[12];
    if (anim_pose_get_matrix(char_id, msid, frame, set->nodes[ni].part_id, m) == 0) {
      batch->state.dynamic_pose_pos_x[di] = m[3];
      batch->state.dynamic_pose_pos_y[di] = m[7];
      batch->state.dynamic_pose_pos_z[di] = m[11];
    } else {
      batch->state.dynamic_pose_pos_x[di] = 0.0f;
      batch->state.dynamic_pose_pos_y[di] = 0.0f;
      batch->state.dynamic_pose_pos_z[di] = 0.0f;
    }
  }
}

static void dynamic_state_step(MslBatch* batch, size_t idx, const MslAnimPoseTable* t,
                               const MslAnimDynSetData* set, uint8_t char_id, uint16_t msid,
                               uint16_t frame, uint8_t collision_owner) {
  if (batch == NULL || t == NULL || set == NULL || set->node_count == 0u) {
    return;
  }
  const uint16_t node_count = set->node_count;
  float base_pos[MSL_MAX_DYNAMIC_NODES][3];
  if (dynamic_node_base_positions(set, char_id, msid, frame, base_pos) != 0) {
    batch->state.dynamic_pose_state_valid[idx] = 0u;
    batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
    return;
  }
  float colliders[ANIM_DYN_MAX_COLLIDERS][4];
  const uint16_t collider_count = dynamic_world_colliders(t, char_id, msid, frame, colliders);

  float prev_pos[MSL_MAX_DYNAMIC_NODES][3];
  for (uint16_t ni = 0; ni < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    prev_pos[ni][0] = batch->state.dynamic_pose_pos_x[di];
    prev_pos[ni][1] = batch->state.dynamic_pose_pos_y[di];
    prev_pos[ni][2] = batch->state.dynamic_pose_pos_z[di];
  }

  for (uint16_t ni = 0; ni < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    float rot[3], pos[3], scl[3];
    if (local_srt_for_part(t, msid, frame, set->nodes[ni].part_id, rot, pos, scl, NULL, NULL) !=
        0) {
      continue;
    }
    batch->state.dynamic_pose_rot_x[di] = rot[0];
    batch->state.dynamic_pose_rot_y[di] = rot[1];
    batch->state.dynamic_pose_rot_z[di] = rot[2];
  }

  // Root node position is directly recomputed from the current JObj matrix each frame. Child node
  // positions are the lb_8001044C carry state.
  {
    const size_t root_di = dynamic_state_index(idx, 0u);
    batch->state.dynamic_pose_pos_x[root_di] = base_pos[0][0];
    batch->state.dynamic_pose_pos_y[root_di] = base_pos[0][1];
    batch->state.dynamic_pose_pos_z[root_di] = base_pos[0][2];
  }

  // SSDYNN01 v8's collision-owner index means this submotion consumes the live dynamic JObj
  // matrix for BODY hurtcaps on every supported frame, even when the current lb_8001044C update
  // resolves to the static segment vector with no nonzero correction carry.
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  uint8_t apply_collision_pose = collision_owner ? 1u : 0u;
  for (uint16_t ni = 0; ni + 1u < node_count; ni++) {
    const size_t di = dynamic_state_index(idx, ni);
    const size_t child_di = dynamic_state_index(idx, (uint16_t)(ni + 1u));

    float parent_pos[3] = {
        batch->state.dynamic_pose_pos_x[di],
        batch->state.dynamic_pose_pos_y[di],
        batch->state.dynamic_pose_pos_z[di],
    };
    float current_dir[3] = {
        base_pos[ni + 1u][0] - base_pos[ni][0],
        base_pos[ni + 1u][1] - base_pos[ni][1],
        base_pos[ni + 1u][2] - base_pos[ni][2],
    };
    const float seg_len = vec3_len(current_dir);
    if (!vec3_normalize(current_dir)) {
      batch->state.dynamic_pose_pos_x[child_di] = base_pos[ni + 1u][0];
      batch->state.dynamic_pose_pos_y[child_di] = base_pos[ni + 1u][1];
      batch->state.dynamic_pose_pos_z[child_di] = base_pos[ni + 1u][2];
      continue;
    }
    float prev_vec[3] = {
        prev_pos[ni + 1u][0] - parent_pos[0],
        prev_pos[ni + 1u][1] - parent_pos[1],
        prev_pos[ni + 1u][2] - parent_pos[2],
    };
    if (!vec3_normalize(prev_vec)) {
      prev_vec[0] = current_dir[0];
      prev_vec[1] = current_dir[1];
      prev_vec[2] = current_dir[2];
    }
    float original_prev[3] = {prev_vec[0], prev_vec[1], prev_vec[2]};

    // Ported shape from lb_8001044C:
    // - node +0x4C and descriptor +0x08 blend previous segment direction toward current_dir.
    // - node +0x8C applies the gravity/down-vector correction derived by lb_80011710 from
    //   descriptor +0x10 / segment length.
    // - node +0x38/+0x44 carries the prior angular correction axis/angle, then +0x84 decays it.
    // - node +0x88 limits same-frame angular movement from the saved link direction.
    // - node +0x50 converges toward natural_dir, and node +0x68 clamps max deviation from it.
    // refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    const float anim_follow = set->nodes[ni].c[0] * set->pos[0];
    if (anim_follow < 1.0f) {
      vec3_rotate_towards(prev_vec, current_dir,
                          vec3_angle(prev_vec, current_dir) * (1.0f - anim_follow));
    }

    const float inv_len_corr = (seg_len > 1.0e-6f) ? (set->pos[2] / seg_len) : 0.0f;
    if (inv_len_corr != 0.0f) {
      const float down[3] = {0.0f, -1.0f, 0.0f};
      const float down_angle = vec3_angle(prev_vec, down);
      if (down_angle > 1.0e-6f) {
        vec3_rotate_towards(prev_vec, down, fabsf(sinf(down_angle) * inv_len_corr));
      }
    }

    const float carry_angle = batch->state.dynamic_pose_angle[di];
    if (fabsf(carry_angle) > 1.0e-6f) {
      float axis[3] = {
          batch->state.dynamic_pose_axis_x[di],
          batch->state.dynamic_pose_axis_y[di],
          batch->state.dynamic_pose_axis_z[di],
      };
      if (vec3_normalize(axis)) {
        float out[3];
        vec3_rotate_about_unit_axis(prev_vec, axis, carry_angle, out);
        if (vec3_normalize(out)) {
          prev_vec[0] = out[0];
          prev_vec[1] = out[1];
          prev_vec[2] = out[2];
        }
      }
    }

    const float converge_angle = fabsf(set->nodes[ni].c[1]);
    if (converge_angle > 0.0f) {
      const float angle_to_natural = vec3_angle(current_dir, prev_vec);
      if (angle_to_natural < converge_angle) {
        prev_vec[0] = current_dir[0];
        prev_vec[1] = current_dir[1];
        prev_vec[2] = current_dir[2];
      } else {
        vec3_rotate_towards(prev_vec, current_dir, converge_angle);
      }
    }

    const float cone = fabsf(set->nodes[ni].c[6]);
    // lb_8001044C applies descriptor +0x68 as a max-deviation cone around the source natural
    // direction after gravity/carry-angle correction. The descriptor natural direction is authored
    // in the same local segment basis as the current JObj chain for Fox's tail set, so clamp against
    // the current segment direction here instead of leaving the already-ported constant unused.
    // refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    if (cone > 0.0f && t->dyn_cone_have_msid != NULL && t->dyn_cone_have_msid[msid]) {
      const float cone_angle = vec3_angle(current_dir, prev_vec);
      if (cone_angle > cone) {
        vec3_rotate_towards(prev_vec, current_dir, cone_angle - cone);
      }
    }

    // ftCo_8009DD94 refreshes fp->x1670 via ftColl_8007AF60 and passes those source dynamic
    // colliders to lb_8001044C before JObj matrices are rebuilt. Source applies this after the
    // max-deviation cone, so collider avoidance can still move a constrained segment away from
    // the fighter body.
    // refs/melee/src/melee/ft/ftdynamics.c::ftCo_8009DD94
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AF60
    // refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    const uint16_t active_collider_count = collider_count;
    for (uint16_t ci = 0; ci < active_collider_count; ci++) {
      const float collider_pos[3] = {colliders[ci][0], colliders[ci][1], colliders[ci][2]};
      const float collider_radius = colliders[ci][3];
      float next_pos[3] = {parent_pos[0] + prev_vec[0] * seg_len,
                           parent_pos[1] + prev_vec[1] * seg_len,
                           parent_pos[2] + prev_vec[2] * seg_len};
      float coll_dir[3] = {collider_pos[0] - parent_pos[0], collider_pos[1] - parent_pos[1],
                           collider_pos[2] - parent_pos[2]};
      float coll_dist = vec3_len(coll_dir);
      if (coll_dist > collider_radius &&
          segment_sphere_intersects(parent_pos, next_pos, collider_pos, kDynColliderSkinRadius,
                                    collider_radius)) {
        float force_dir[3] = {coll_dir[0], coll_dir[1], coll_dir[2]};
        if (!vec3_normalize(force_dir)) {
          continue;
        }
        const float coll_angle = vec3_angle(force_dir, prev_vec);
        if (coll_angle <= 1.0e-6f) {
          continue;
        }
        const float adj_radius = kDynColliderSkinRadius + collider_radius;
        float side_sq = coll_dist * coll_dist - adj_radius * adj_radius;
        if (side_sq < 0.0f) {
          side_sq = 0.0f;
        }
        const float side = sqrtf(side_sq);
        const float avoidance_angle = fabsf(atan2f(adj_radius, side)) - coll_angle;
        if (avoidance_angle > 0.0f) {
          float axis[3];
          vec3_cross(force_dir, prev_vec, axis);
          if (vec3_normalize(axis)) {
            float out[3];
            vec3_rotate_about_unit_axis(prev_vec, axis, avoidance_angle, out);
            if (vec3_normalize(out)) {
              prev_vec[0] = out[0];
              prev_vec[1] = out[1];
              prev_vec[2] = out[2];
            }
          }
        }
      }
    }

    float carry_axis[3];
    vec3_cross(original_prev, prev_vec, carry_axis);
    float next_carry = 0.0f;
    if (vec3_normalize(carry_axis)) {
      next_carry = vec3_angle(original_prev, prev_vec);
    } else {
      carry_axis[0] = 1.0f;
      carry_axis[1] = 0.0f;
      carry_axis[2] = 0.0f;
    }
    const float decay = fabsf(set->nodes[ni].c[13]);
    if (next_carry > decay) {
      next_carry -= decay;
    } else {
      next_carry = 0.0f;
    }
    if (next_carry > 1.0e-6f) {
      if (collision_owner) {
        apply_collision_pose = 1u;
      }
    }
    batch->state.dynamic_pose_axis_x[di] = carry_axis[0];
    batch->state.dynamic_pose_axis_y[di] = carry_axis[1];
    batch->state.dynamic_pose_axis_z[di] = carry_axis[2];
    batch->state.dynamic_pose_angle[di] = next_carry;

    batch->state.dynamic_pose_pos_x[child_di] = parent_pos[0] + prev_vec[0] * seg_len;
    batch->state.dynamic_pose_pos_y[child_di] = parent_pos[1] + prev_vec[1] * seg_len;
    batch->state.dynamic_pose_pos_z[child_di] = parent_pos[2] + prev_vec[2] * seg_len;
  }
  batch->state.dynamic_pose_state_valid[idx] = 1u;
  batch->state.dynamic_pose_apply_collision_matrix[idx] = apply_collision_pose;
  batch->state.dynamic_pose_node_count[idx] = (uint8_t)node_count;
  batch->state.dynamic_pose_char_id[idx] = char_id;
  batch->state.dynamic_pose_msid[idx] = msid;
  batch->state.dynamic_pose_frame[idx] = frame;
}

void anim_pose_update_dynamic_state(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t char_id = batch->state.char_id[idx];
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      const uint16_t msid = (uint16_t)anim_u32;
      const MslAnimPoseTable* t = table_for_char(char_id);
      if (t == NULL || t->dyn_set_count == 0u) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      const MslAnimDynSetData* set = &t->dyn_sets[0];
      if (set->node_count == 0u || !t->local_have_msid || !t->local_have_msid[msid]) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      const uint8_t collision_owner =
          (t->dyn_collision_have_msid != NULL && t->dyn_collision_have_msid[msid]) ? 1u : 0u;
      const uint8_t catch_grabbable_owner =
          (t->dyn_catch_grabbable_have_msid != NULL && t->dyn_catch_grabbable_have_msid[msid]) ? 1u
                                                                                               : 0u;
      if (!collision_owner && !catch_grabbable_owner) {
        batch->state.dynamic_pose_state_valid[idx] = 0u;
        batch->state.dynamic_pose_apply_collision_matrix[idx] = 0u;
        continue;
      }
      float dynamic_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      const uint16_t frame = msl_anim_frame_floor_u16(dynamic_frame_f32);
      const uint8_t same_msid_sequential =
          (batch->state.dynamic_pose_state_valid[idx] &&
           batch->state.dynamic_pose_char_id[idx] == char_id &&
           batch->state.dynamic_pose_msid[idx] == msid &&
           (uint16_t)(batch->state.dynamic_pose_frame[idx] + 1u) == frame)
              ? 1u
              : 0u;
      if (!same_msid_sequential) {
        dynamic_state_initialize_from_locals(batch, idx, t, set, char_id, msid, 0u);
        const uint16_t max_frame = (frame < t->local_frame_count_by_msid[msid])
                                       ? frame
                                       : (uint16_t)(t->local_frame_count_by_msid[msid] - 1u);
        for (uint16_t f = 0u; f <= max_frame; f++) {
          dynamic_state_step(batch, idx, t, set, char_id, msid, f, collision_owner);
          if (f == 0xFFFFu) {
            break;
          }
        }
      } else {
        dynamic_state_step(batch, idx, t, set, char_id, msid, frame, collision_owner);
      }
    }
  }
}

static int dynamic_matrix_from_locals(const MslBatch* batch, size_t player_idx,
                                      const MslAnimPoseTable* t, const MslAnimDynSetData* set,
                                      uint16_t msid, uint16_t frame, uint16_t part_id,
                                      uint8_t dynamic_matrix_mode, float out_3x4[12]) {
  enum { MAX_PATH = 96 };
  uint16_t path[MAX_PATH];
  uint16_t count = 0;
  uint16_t cur = part_id;
  for (;;) {
    if (count >= (uint16_t)MAX_PATH) {
      return -1;
    }
    float rot[3], pos[3], scl[3];
    int16_t parent = -1;
    if (local_srt_for_part(t, msid, frame, cur, rot, pos, scl, NULL, &parent) != 0) {
      return -1;
    }
    path[count++] = cur;
    if (parent < 0) {
      break;
    }
    cur = (uint16_t)parent;
  }

  float world[12];
  mtx34_identity(world);
  float parent_world_scl[3] = {0.0f, 0.0f, 0.0f};
  uint8_t have_parent_scl = 0u;
  for (int i = (int)count - 1; i >= 0; i--) {
    const uint16_t part = path[i];
    float rot[3], pos[3], scl[3];
    uint32_t flags = 0;
    int16_t parent = -1;
    if (local_srt_for_part(t, msid, frame, part, rot, pos, scl, &flags, &parent) != 0) {
      return -1;
    }
    const int dyn_i = dynamic_set_node_index_for_part(set, part);
    const float* parent_scl = NULL;
    if (parent >= 0 && have_parent_scl) {
      parent_scl = parent_world_scl;
    }
    float local[12];
    mtx34_srt_simple(rot, pos, scl, parent_scl, local);
    mtx34_concat(world, local, world);
    if (batch != NULL && dynamic_matrix_mode != 0u && dyn_i >= 0 &&
        (uint16_t)dyn_i < (uint16_t)MSL_MAX_DYNAMIC_NODES) {
      const uint16_t dyn_u = (uint16_t)dyn_i;
      const size_t di = dynamic_state_index(player_idx, dyn_u);
      if (dyn_u + 1u < batch->state.dynamic_pose_node_count[player_idx]) {
        const size_t child_di = dynamic_state_index(player_idx, (uint16_t)(dyn_u + 1u));
        float base_parent[12], base_child[12];
        if (anim_pose_get_matrix(batch->state.dynamic_pose_char_id[player_idx], msid, frame, part,
                                 base_parent) == 0 &&
            anim_pose_get_matrix(batch->state.dynamic_pose_char_id[player_idx], msid, frame,
                                 set->nodes[dyn_u + 1u].part_id, base_child) == 0) {
          float base_dir[3] = {
              base_child[3] - base_parent[3],
              base_child[7] - base_parent[7],
              base_child[11] - base_parent[11],
          };
          float dyn_dir[3] = {
              batch->state.dynamic_pose_pos_x[child_di] - batch->state.dynamic_pose_pos_x[di],
              batch->state.dynamic_pose_pos_y[child_di] - batch->state.dynamic_pose_pos_y[di],
              batch->state.dynamic_pose_pos_z[child_di] - batch->state.dynamic_pose_pos_z[di],
          };
          if (vec3_normalize(base_dir) && vec3_normalize(dyn_dir)) {
            float axis[3];
            vec3_cross(base_dir, dyn_dir, axis);
            if (vec3_normalize(axis)) {
              const float angle = vec3_angle(base_dir, dyn_dir);
              if (angle > 1.0e-6f) {
                mtx34_apply_world_axis_angle(world, axis, angle);
              }
            }
          }
        }
      }
      if (dynamic_matrix_mode == 2u) {
        world[3] = batch->state.dynamic_pose_pos_x[di];
        world[7] = batch->state.dynamic_pose_pos_y[di];
        world[11] = batch->state.dynamic_pose_pos_z[di];
      }
    }

    if ((flags & 8u) != 0u) {
      if (parent >= 0 && have_parent_scl) {
        // The path is a single root-to-part chain, so the only future scale consumer is this
        // node's direct child in the same chain.
        have_parent_scl = 1u;
      } else {
        have_parent_scl = 0u;
      }
    } else {
      if (parent >= 0 && have_parent_scl) {
        parent_world_scl[0] *= scl[0];
        parent_world_scl[1] *= scl[1];
        parent_world_scl[2] *= scl[2];
      } else {
        memcpy(parent_world_scl, scl, 3u * sizeof(float));
      }
      have_parent_scl = 1u;
    }
  }
  memcpy(out_3x4, world, MAT_BYTES);
  return 0;
}

static int local_parent_for_part(const MslAnimPoseTable* t, uint16_t part_id, int16_t* out_parent);

static int matrix_from_locals_f32(const MslAnimPoseTable* t, uint16_t msid, float anim_frame,
                                  uint16_t part_id, float out_3x4[12]) {
  if (t == NULL || out_3x4 == NULL) {
    return -1;
  }
  enum { MAX_PATH = 96 };
  uint16_t path[MAX_PATH];
  uint16_t count = 0;
  uint16_t cur = part_id;
  for (;;) {
    if (count >= (uint16_t)MAX_PATH) {
      return -1;
    }
    int16_t parent = -1;
    // Parent identity is invariant across animation frames and already extracted in SSANIML1.
    // The old path interpreted every FObj track here only to discard its SRT, then interpreted the
    // same chain again below to build the matrix. Keep the sole value-producing interpretation in
    // the root-to-part pass and use the extracted hierarchy for this path discovery pass.
    // data/anim/*.locals.bin::SSANIML1 local_parent
    if (local_parent_for_part(t, cur, &parent) != 0) {
      return -1;
    }
    path[count++] = cur;
    if (parent < 0) {
      break;
    }
    cur = (uint16_t)parent;
  }

  float world[12];
  mtx34_identity(world);
  float parent_world_scl[3] = {0.0f, 0.0f, 0.0f};
  uint8_t have_parent_scl = 0u;
  for (int i = (int)count - 1; i >= 0; i--) {
    const uint16_t part = path[i];
    float rot[3], pos[3], scl[3];
    uint32_t flags = 0;
    int16_t parent = -1;
    if (local_srt_for_part_f32(t, msid, anim_frame, part, rot, pos, scl, &flags, &parent) != 0) {
      return -1;
    }
    const float* parent_scl = NULL;
    if (parent >= 0 && have_parent_scl) {
      parent_scl = parent_world_scl;
    }
    float local[12];
    mtx34_srt_simple(rot, pos, scl, parent_scl, local);
    mtx34_concat(world, local, world);

    if ((flags & 8u) != 0u) {
      if (parent >= 0 && have_parent_scl) {
        have_parent_scl = 1u;
      } else {
        have_parent_scl = 0u;
      }
    } else {
      if (parent >= 0 && have_parent_scl) {
        parent_world_scl[0] *= scl[0];
        parent_world_scl[1] *= scl[1];
        parent_world_scl[2] *= scl[2];
      } else {
        memcpy(parent_world_scl, scl, 3u * sizeof(float));
      }
      have_parent_scl = 1u;
    }
  }
  memcpy(out_3x4, world, MAT_BYTES);
  return 0;
}

int anim_pose_get_collision_matrix(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                   uint16_t frame, uint16_t part_id, float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  if (batch == NULL) {
    return -1;
  }
  const uint8_t char_id = batch->state.char_id[player_idx];
  if (anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4) != 0) {
    return -1;
  }
  if (!batch->state.dynamic_pose_apply_collision_matrix[player_idx]) {
    return 0;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL || t->dyn_collision_have_msid == NULL || !t->dyn_collision_have_msid[msid]) {
    return 0;
  }
  const MslAnimDynSetData* set = dynamic_set_for_part(t, part_id);
  if (set == NULL) {
    return 0;
  }
  if (batch->state.dynamic_pose_char_id[player_idx] != char_id ||
      batch->state.dynamic_pose_msid[player_idx] != msid) {
    return 0;
  }
  float dyn[12];
  if (dynamic_matrix_from_locals(batch, player_idx, t, set, msid, frame, part_id,
                                 /*dynamic_matrix_mode=*/1u, dyn) == 0) {
    memcpy(out_3x4, dyn, MAT_BYTES);
  }
  return 0;
}

static int anim_pose_get_collision_matrix_f32_base(const MslBatch* batch, size_t player_idx,
                                                   uint16_t msid, float anim_frame,
                                                   uint16_t part_id, float out_3x4[12]) {
  if (out_3x4 == NULL || batch == NULL) {
    return -1;
  }
  const uint8_t char_id = batch->state.char_id[player_idx];
  const float safe_frame = isfinite(anim_frame) ? anim_frame : 0.0f;
  const uint16_t frame = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(safe_frame));
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  const uint16_t frame_count = (t->have_msid[msid] != 0u) ? t->frame_count_by_msid[msid] : 0u;
  if (fabsf(safe_frame - (float)frame) <= 1.0e-6f && frame < frame_count) {
    return anim_pose_get_collision_matrix(batch, player_idx, msid, frame, part_id, out_3x4);
  }

  // HSD_AObjInterpretAnim drives JObj local SRT from float `curr_frame`; lb_8000B1CC then samples
  // the updated JObj matrix for collision primitives before ftColl admission.
  //
  // Some non-looping AObjs have an extracted FObj `end_frame` longer than the baked SSANIM01
  // integer matrix/local table (for example Falco Wait1_0: tracks end at 240 while matrices cover
  // 120 frames). GALE01 still evaluates the live FObj tracks through the later frames; collision
  // must use that source track pose instead of treating the BODY capsules as absent.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  if (matrix_from_locals_f32(t, msid, safe_frame, part_id, out_3x4) != 0) {
    if (frame >= frame_count || anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4) != 0) {
      return -1;
    }
  }

  if (!batch->state.dynamic_pose_apply_collision_matrix[player_idx]) {
    return 0;
  }
  if (t->dyn_collision_have_msid == NULL || !t->dyn_collision_have_msid[msid]) {
    return 0;
  }
  const MslAnimDynSetData* set = dynamic_set_for_part(t, part_id);
  if (set == NULL) {
    return 0;
  }
  if (batch->state.dynamic_pose_char_id[player_idx] != char_id ||
      batch->state.dynamic_pose_msid[player_idx] != msid) {
    return 0;
  }
  float dyn[12];
  if (dynamic_matrix_from_locals(batch, player_idx, t, set, msid, frame, part_id,
                                 /*dynamic_matrix_mode=*/1u, dyn) == 0) {
    memcpy(out_3x4, dyn, MAT_BYTES);
  }
  return 0;
}

static uint8_t anim_pose_common_fall_target_msid(const MslBatch* batch, size_t player_idx,
                                                 uint16_t msid, uint16_t* out_target_msid,
                                                 float* out_weight) {
  if (batch == NULL || out_target_msid == NULL || out_weight == NULL) {
    return 0u;
  }
  uint16_t neutral = 0u;
  uint16_t forwards = 0u;
  uint16_t backwards = 0u;
  if (!msl_action_common_fall_blend_msids(batch->state.action_id[player_idx], &neutral, &forwards,
                                          &backwards)) {
    return 0u;
  }
  if (msid != neutral && msid != forwards && msid != backwards) {
    return 0u;
  }
  const float x4 = batch->state.common_fall_blend_x4[player_idx];
  if (x4 == 0.0f) {
    return 0u;
  }
  const uint16_t stored_msid = batch->state.common_fall_blend_msid[player_idx];
  if (stored_msid != neutral && stored_msid != forwards && stored_msid != backwards) {
    return 0u;
  }
  *out_target_msid = stored_msid;
  *out_weight = (x4 > 1.0f) ? 1.0f : x4;
  return 1u;
}

static int local_parent_for_part(const MslAnimPoseTable* t, uint16_t part_id, int16_t* out_parent) {
  if (t == NULL || out_parent == NULL || t->local_part_to_index == NULL ||
      t->local_parent_part_by_index == NULL) {
    return -1;
  }
  const uint16_t li = t->local_part_to_index[part_id];
  if (li == 0xFFFFu || li >= t->local_count) {
    return -1;
  }
  *out_parent = t->local_parent_part_by_index[li];
  return 0;
}

// Alternate-submotion frame consumed by the CommonFall blend. ftAnim_8006EDD0 loads the
// FallF/FallB skeleton at the switch-time cur_anim_frame, and each HSD_JObjAnimAll evaluates it
// then advances its AObj by 1 with the loop rewind (frame >= end -> frame -= end): twice on the
// smid-switch frame (ftCo_Fall_Anim_Inner + ftCo_800CC988), once per frame afterwards. Relative
// to the base cur_anim_frame (which advances on the same 0..end-1 cycle) this is a constant +1
// offset. JObj blend probes: PutridJoyousOryx f5257 alt evals 1.0/2.0 (reload) and f5263 alt 0.0
// vs base 7.0; ElatedWearyTermite f5843 alt 5.0 vs base 4.0; FumblingSaneBeaver f5055 alt evals
// 1.0/2.0 (reload) and f5056 alt 3.0 vs base 2.0.
// refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006EDD0,ftAnim_8006FE9C,ftAnim_8006FF74}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Anim_Inner,ftCo_800CC988}
static float common_fall_blend_alt_anim_frame(const MslAnimPoseTable* t, uint16_t target_msid,
                                              float anim_frame) {
  float alt = anim_frame + 1.0f;
  if (t != NULL && t->local_have_msid != NULL && t->local_frame_count_by_msid != NULL &&
      t->local_have_msid[target_msid]) {
    const uint16_t fc = t->local_frame_count_by_msid[target_msid];
    if (fc > 1u) {
      const float end = (float)(fc - 1u);
      if (alt >= end) {
        alt -= end;
      }
    }
  }
  return alt;
}

static int common_fall_blended_local_matrix(const MslAnimPoseTable* t, uint16_t neutral_msid,
                                            uint16_t target_msid, float anim_frame,
                                            uint16_t part_id, float weight, uint8_t reload,
                                            const float* parent_scl, float out[12],
                                            float out_scl[3], uint32_t* out_flags,
                                            int16_t* out_parent) {
  const float alt_frame = common_fall_blend_alt_anim_frame(t, target_msid, anim_frame);
  if (out_flags == NULL || out_parent == NULL) {
    return -1;
  }
  enum { MSL_FTPART_TRANSN = 1, MSL_FTPART_FLAGS_B4_COPY = 0x35 };
  if (part_id <= (uint16_t)MSL_FTPART_TRANSN || part_id == (uint16_t)MSL_FTPART_FLAGS_B4_COPY) {
    // ftAnim_8006FE9C starts at FtPart_TransN. Ancestors such as TopN keep the active selected
    // submotion JObj written by ftAnim_8006EDD0 / HSD_JObjAnimAll, and ftParts marks TransN and
    // part 0x35 flags_b4, which routes them through lbCopyJObjSRT (a weight-independent full
    // copy of the alternate pose) instead of the lb_8000C490 blend (JObj blend probe: exactly
    // two lbCopyJObjSRT calls per blend pass on PutridJoyousOryx f5263).
    // refs/melee/src/melee/ft/forward.h::{FtPart_TopN,FtPart_TransN}
    // refs/melee/src/melee/ft/ftparts.c::ftParts_80074148 (flags_b4 init)
    // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FE9C
    // refs/melee/src/melee/lb/lb_00B0.c::lbCopyJObjSRT
    float rot[3], pos[3], scl[3];
    if (local_srt_for_part_f32(t, target_msid, alt_frame, part_id, rot, pos, scl, out_flags,
                               out_parent) != 0) {
      return -1;
    }
    memcpy(out_scl, scl, 3u * sizeof(float));
    mtx34_srt_simple(rot, pos, scl, parent_scl, out);
    return 0;
  }
  float neutral_rot[3], neutral_pos[3], neutral_scl[3];
  float target_rot[3], target_pos[3], target_scl[3];
  int16_t neutral_parent = -1;
  int16_t target_parent = -1;
  uint32_t neutral_flags = 0u;
  uint32_t target_flags = 0u;
  if (local_srt_for_part_f32(t, neutral_msid, anim_frame, part_id, neutral_rot, neutral_pos,
                             neutral_scl, &neutral_flags, &neutral_parent) != 0 ||
      local_srt_for_part_f32(t, target_msid, alt_frame, part_id, target_rot, target_pos, target_scl,
                             &target_flags, &target_parent) != 0 ||
      neutral_parent != target_parent || neutral_flags != target_flags) {
    return -1;
  }
  *out_flags = neutral_flags;
  *out_parent = neutral_parent;

  const float inv = 1.0f - weight;
  float pos[3], scl[3];
  MslQuat target_q;
  MslQuat neutral_q;
  MslQuat blended_q;
  quat_from_euler_srt_order(neutral_rot, &neutral_q);
  if (reload) {
    // ftAnim_8006EDD0 reload frame: ftCo_Fall_Anim_Inner's own ftAnim_8006FE9C pass blends the
    // fresh base pose with the alternate at its load frame (the base cur_anim_frame), the alt
    // AObj advances, and ftCo_800CC988's pass blends that in-place result with the alternate at
    // the +1 frame. JObj blend probe PositiveRevolvingHyena f8015: two lb_8000C490 passes with
    // identical x4, pass-2's base quaternion equals pass-1's output exactly.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Anim_Inner,ftCo_800CC988}
    float p1_rot[3], p1_pos[3], p1_scl[3];
    uint32_t p1_flags = 0u;
    int16_t p1_parent = -1;
    if (local_srt_for_part_f32(t, target_msid, anim_frame, part_id, p1_rot, p1_pos, p1_scl,
                               &p1_flags, &p1_parent) != 0 ||
        p1_parent != neutral_parent || p1_flags != neutral_flags) {
      return -1;
    }
    float stage_pos[3], stage_scl[3];
    for (int i = 0; i < 3; i++) {
      stage_pos[i] = p1_pos[i] * weight + neutral_pos[i] * inv;
      stage_scl[i] = p1_scl[i] * weight + neutral_scl[i] * inv;
      pos[i] = target_pos[i] * weight + stage_pos[i] * inv;
      scl[i] = target_scl[i] * weight + stage_scl[i] * inv;
      out_scl[i] = scl[i];
    }
    MslQuat p1_q;
    MslQuat stage_q;
    quat_from_euler_srt_order(p1_rot, &p1_q);
    quat_slerp_lb_c490(&p1_q, &neutral_q, inv, &stage_q);
    quat_from_euler_srt_order(target_rot, &target_q);
    quat_slerp_lb_c490(&target_q, &stage_q, inv, &blended_q);
    mtx34_quat_srt_simple(&blended_q, pos, scl, parent_scl, out);
    return 0;
  }
  for (int i = 0; i < 3; i++) {
    // ftAnim_8006FE9C calls lb_8000C490(x4_jobj2, joint, joint, x4, 1-x4). Position and scale
    // are linear SRT blends; rotation follows lb_8000C490's quaternion path below.
    pos[i] = target_pos[i] * weight + neutral_pos[i] * inv;
    scl[i] = target_scl[i] * weight + neutral_scl[i] * inv;
    out_scl[i] = scl[i];
  }
  quat_from_euler_srt_order(target_rot, &target_q);
  quat_slerp_lb_c490(&target_q, &neutral_q, inv, &blended_q);
  mtx34_quat_srt_simple(&blended_q, pos, scl, parent_scl, out);
  return 0;
}

static int matrix_from_common_fall_blended_locals(const MslAnimPoseTable* t, uint16_t neutral_msid,
                                                  uint16_t target_msid, float anim_frame,
                                                  uint16_t part_id, float weight, uint8_t reload,
                                                  float out_3x4[12]) {
  if (weight == 1.0f) {
    // ftCo_Fall_Anim_Inner calls ftAnim_8006FF74, not ftAnim_8006FE9C, once x4 reaches 1.0.
    // That path copies the selected FallF/FallB submotion pose instead of running lb_8000C490,
    // still at the alternate skeleton's own (+1 offset) frame.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
    // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FF74
    return matrix_from_locals_f32(t, target_msid,
                                  common_fall_blend_alt_anim_frame(t, target_msid, anim_frame),
                                  part_id, out_3x4);
  }
  if (weight == 0.0f) {
    return matrix_from_locals_f32(t, neutral_msid, anim_frame, part_id, out_3x4);
  }
  enum { MAX_CHAIN = 64 };
  uint16_t chain[MAX_CHAIN];
  uint16_t count = 0u;
  uint16_t cur = part_id;
  for (;;) {
    if (count >= (uint16_t)MAX_CHAIN) {
      return -1;
    }
    chain[count++] = cur;
    int16_t parent = -1;
    if (local_parent_for_part(t, cur, &parent) != 0) {
      return -1;
    }
    if (parent < 0) {
      break;
    }
    cur = (uint16_t)parent;
  }

  float world[12];
  mtx34_identity(world);
  float parent_world_scl[3] = {0.0f, 0.0f, 0.0f};
  uint8_t have_parent_scl = 0u;
  for (int ci = (int)count - 1; ci >= 0; ci--) {
    float local[12];
    float scl[3];
    uint32_t flags = 0u;
    int16_t parent = -1;
    const float* parent_scl = NULL;
    if (local_parent_for_part(t, chain[ci], &parent) != 0) {
      return -1;
    }
    if (parent >= 0 && have_parent_scl) {
      parent_scl = parent_world_scl;
    }
    if (common_fall_blended_local_matrix(t, neutral_msid, target_msid, anim_frame, chain[ci],
                                         weight, reload, parent_scl, local, scl, &flags,
                                         &parent) != 0) {
      return -1;
    }
    mtx34_concat(world, local, world);

    if ((flags & 8u) != 0u) {
      if (parent >= 0 && have_parent_scl) {
        have_parent_scl = 1u;
      } else {
        have_parent_scl = 0u;
      }
    } else {
      if (parent >= 0 && have_parent_scl) {
        parent_world_scl[0] *= scl[0];
        parent_world_scl[1] *= scl[1];
        parent_world_scl[2] *= scl[2];
      } else {
        memcpy(parent_world_scl, scl, 3u * sizeof(float));
      }
      have_parent_scl = 1u;
    }
  }
  memcpy(out_3x4, world, MAT_BYTES);
  return 0;
}

int anim_pose_debug_common_fall_blend_matrix(uint8_t char_id, uint16_t neutral_msid,
                                             uint16_t target_msid, float anim_frame,
                                             uint16_t part_id, float weight, float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  if (weight < 0.0f) {
    weight = 0.0f;
  } else if (weight > 1.0f) {
    weight = 1.0f;
  }
  return matrix_from_common_fall_blended_locals(t, neutral_msid, target_msid, anim_frame, part_id,
                                                weight, /*reload=*/0u, out_3x4);
}

int anim_pose_debug_collision_matrix_f32(uint8_t char_id, uint16_t msid, float anim_frame,
                                         uint16_t part_id, float out_3x4[12]) {
  if (out_3x4 == NULL) {
    return -1;
  }
  const float safe_frame = isfinite(anim_frame) ? anim_frame : 0.0f;
  const uint16_t frame = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(safe_frame));
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  const uint16_t frame_count = (t->have_msid[msid] != 0u) ? t->frame_count_by_msid[msid] : 0u;
  if (fabsf(safe_frame - (float)frame) <= 1.0e-6f && frame < frame_count) {
    return anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4);
  }
  if (matrix_from_locals_f32(t, msid, safe_frame, part_id, out_3x4) != 0) {
    if (frame >= frame_count || anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4) != 0) {
      return -1;
    }
  }
  return 0;
}

int anim_pose_get_common_fall_blend_collision_matrix_f32(const MslBatch* batch, size_t player_idx,
                                                         uint16_t msid, float anim_frame,
                                                         uint16_t part_id, float out_3x4[12]) {
  if (batch == NULL || out_3x4 == NULL) {
    return -1;
  }
  uint16_t target_msid = 0u;
  float weight = 0.0f;
  if (!anim_pose_common_fall_target_msid(batch, player_idx, msid, &target_msid, &weight)) {
    return -1;
  }
  const uint8_t char_id = batch->state.char_id[player_idx];
  const MslAnimPoseTable* t = table_for_char(char_id);
  const uint8_t reload = batch->state.common_fall_blend_reload[player_idx];
  if (t == NULL || matrix_from_common_fall_blended_locals(t, msid, target_msid, anim_frame, part_id,
                                                          weight, reload, out_3x4) != 0) {
    return -1;
  }
  // ftCo_800CC988 / ftCo_Fall_Anim_Inner run the selected Fall/FallF/FallB submotion, then call
  // ftAnim_8006FE9C(fp, FtPart_TransN, x4, 1-x4). ftAnim_8006FE9C leaves pre-TransN ancestors on
  // the active selected JObj tree and uses lb_8000C490 to blend TransN descendants in local SRT
  // before collision refresh samples matrices in Fighter_8006CB94. Rebuild the selected part
  // through that local owner instead of interpolating final 3x4 matrices.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
  //   ftCo_800CC988,ftCo_Fall_Anim_Inner}
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FE9C
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000C490
  return 0;
}

int anim_pose_get_collision_matrix_f32(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                       float anim_frame, uint16_t part_id, float out_3x4[12]) {
  if (anim_pose_get_common_fall_blend_collision_matrix_f32(batch, player_idx, msid, anim_frame,
                                                           part_id, out_3x4) == 0) {
    return 0;
  }
  return anim_pose_get_collision_matrix_f32_base(batch, player_idx, msid, anim_frame, part_id,
                                                 out_3x4);
}

int anim_pose_get_catch_grabbable_matrix_f32(const MslBatch* batch, size_t player_idx,
                                             uint16_t msid, float anim_frame, uint16_t part_id,
                                             float out_3x4[12]) {
  if (out_3x4 == NULL || batch == NULL) {
    return -1;
  }
  const uint8_t char_id = batch->state.char_id[player_idx];
  const float safe_frame = isfinite(anim_frame) ? anim_frame : 0.0f;
  const uint16_t frame = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(safe_frame));
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  const uint16_t frame_count = (t->have_msid[msid] != 0u) ? t->frame_count_by_msid[msid] : 0u;
  const uint8_t catch_dynamic_pose =
      (batch->state.dynamic_pose_state_valid[player_idx] != 0u &&
       batch->state.dynamic_pose_char_id[player_idx] == char_id &&
       batch->state.dynamic_pose_msid[player_idx] == msid &&
       (t->dyn_catch_grabbable_have_msid != NULL && t->dyn_catch_grabbable_have_msid[msid]))
          ? 1u
          : 0u;
  if (catch_dynamic_pose == 0u) {
    return -1;
  }
  if (matrix_from_locals_f32(t, msid, safe_frame, part_id, out_3x4) != 0) {
    if (frame >= frame_count || anim_pose_get_matrix(char_id, msid, frame, part_id, out_3x4) != 0) {
      return -1;
    }
  }
  const MslAnimDynSetData* set = dynamic_set_for_part(t, part_id);
  if (set == NULL) {
    return 0;
  }
  float dyn[12];
  if (dynamic_matrix_from_locals(batch, player_idx, t, set, msid, frame, part_id,
                                 /*dynamic_matrix_mode=*/2u, dyn) == 0) {
    memcpy(out_3x4, dyn, MAT_BYTES);
  }
  return 0;
}

int anim_pose_get_collision_matrices_f32(const MslBatch* batch, size_t player_idx, uint16_t msid,
                                         float anim_frame, const uint16_t* part_ids, uint16_t count,
                                         float* out_mats_12, uint8_t* out_ok) {
  if (out_ok != NULL) {
    for (uint16_t i = 0; i < count; i++) {
      out_ok[i] = 0u;
    }
  }
  if (batch == NULL || part_ids == NULL || out_mats_12 == NULL || out_ok == NULL) {
    return -1;
  }
  if (count == 0u) {
    return 0;
  }

  const uint8_t char_id = batch->state.char_id[player_idx];
  const float safe_frame = isfinite(anim_frame) ? anim_frame : 0.0f;
  const uint16_t frame = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(safe_frame));
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL || !t->have_msid[msid]) {
    return -1;
  }
  const uint16_t frame_count = t->frame_count_by_msid[msid];
  const uint8_t have_baked_matrix_frame = (frame < frame_count) ? 1u : 0u;

  const uint8_t integer_frame = (fabsf(safe_frame - (float)frame) <= 1.0e-6f) ? 1u : 0u;
  const uint8_t dynamic_collision_pose =
      (batch->state.dynamic_pose_apply_collision_matrix[player_idx] != 0u &&
       t->dyn_collision_have_msid != NULL && t->dyn_collision_have_msid[msid] &&
       batch->state.dynamic_pose_char_id[player_idx] == char_id &&
       batch->state.dynamic_pose_msid[player_idx] == msid)
          ? 1u
          : 0u;

  if (integer_frame != 0u && dynamic_collision_pose == 0u && have_baked_matrix_frame != 0u) {
    const uint32_t base_off = t->base_off_by_msid[msid];
    const uint64_t joint_count_u = (uint64_t)t->joint_count;
    const uint64_t frame_u = (uint64_t)frame;
    const uint64_t frame_base_u =
        (uint64_t)base_off + frame_u * joint_count_u * (uint64_t)MAT_BYTES;
    if (frame_base_u + joint_count_u * (uint64_t)MAT_BYTES > (uint64_t)t->sz) {
      return -1;
    }
    for (uint16_t i = 0; i < count; i++) {
      const uint16_t joint_index = t->part_to_joint_index[part_ids[i]];
      if (joint_index == 0xFFFFu || joint_index >= t->joint_count) {
        continue;
      }
      const uint64_t mat_off_u = frame_base_u + (uint64_t)joint_index * (uint64_t)MAT_BYTES;
      memcpy(&out_mats_12[(size_t)i * 12u], t->buf + (size_t)mat_off_u, (size_t)MAT_BYTES);
      (void)anim_pose_get_common_fall_blend_collision_matrix_f32(
          batch, player_idx, msid, safe_frame, part_ids[i], &out_mats_12[(size_t)i * 12u]);
      out_ok[i] = 1u;
    }
    return 0;
  }

  if (dynamic_collision_pose == 0u) {
    for (uint16_t i = 0; i < count; i++) {
      float* out = &out_mats_12[(size_t)i * 12u];
      if (matrix_from_locals_f32(t, msid, safe_frame, part_ids[i], out) != 0) {
        if (have_baked_matrix_frame == 0u ||
            anim_pose_get_matrix(char_id, msid, frame, part_ids[i], out) != 0) {
          continue;
        }
      }
      (void)anim_pose_get_common_fall_blend_collision_matrix_f32(batch, player_idx, msid,
                                                                 safe_frame, part_ids[i], out);
      out_ok[i] = 1u;
    }
    return 0;
  }

  for (uint16_t i = 0; i < count; i++) {
    if (anim_pose_get_collision_matrix_f32(batch, player_idx, msid, safe_frame, part_ids[i],
                                           &out_mats_12[(size_t)i * 12u]) == 0) {
      out_ok[i] = 1u;
    }
  }
  return 0;
}

int anim_pose_get_transn(uint8_t char_id, uint16_t msid, uint16_t frame, float out_xyz[3]) {
  if (out_xyz == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL) {
    return -1;
  }
  if (!t->have_msid[msid]) {
    return -1;
  }

  const uint16_t frame_count = t->frame_count_by_msid[msid];
  if (frame >= frame_count) {
    return -1;
  }

  const uint32_t base_off = t->base_off_by_msid[msid];
  const uint64_t joint_count_u = (uint64_t)t->joint_count;
  const uint64_t frame_count_u = (uint64_t)frame_count;
  const uint64_t frame_u = (uint64_t)frame;

  const uint64_t mats_bytes = frame_count_u * joint_count_u * (uint64_t)MAT_BYTES;
  const uint64_t transn_off_u =
      (uint64_t)base_off + mats_bytes + frame_u * (uint64_t)TRANSN_BYTES_PER_FRAME;
  if (transn_off_u + (uint64_t)TRANSN_BYTES_PER_FRAME > (uint64_t)t->sz) {
    return -1;
  }

  memcpy(out_xyz, t->buf + (size_t)transn_off_u, (size_t)TRANSN_BYTES_PER_FRAME);
  return 0;
}

int anim_pose_get_transn_f32(uint8_t char_id, uint16_t msid, float anim_frame, float out_xyz[3]) {
  enum { MSL_FTPART_TRANSN = 1 };
  if (out_xyz == NULL) {
    return -1;
  }
  const MslAnimPoseTable* t = table_for_char(char_id);
  if (t == NULL || !t->have_msid[msid]) {
    return -1;
  }

  const float safe_frame = msl_anim_frame_sanitize_f32(anim_frame);
  if (t->track_buf != NULL && t->track_msid_to_anim_index != NULL &&
      t->track_part_to_index != NULL && t->track_part_record_off_by_anim_li != NULL) {
    const uint16_t ai = t->track_msid_to_anim_index[msid];
    const uint16_t li = t->track_part_to_index[(uint16_t)MSL_FTPART_TRANSN];
    if (ai != 0xFFFFu && ai < t->track_anim_count && li != 0xFFFFu && li < t->track_local_count) {
      const uint32_t rec_off =
          t->track_part_record_off_by_anim_li[(size_t)ai * (size_t)t->track_local_count +
                                              (size_t)li];
      if ((uint64_t)rec_off + 2u <= (uint64_t)t->track_sz &&
          t->track_buf[(size_t)rec_off + 0u] == (uint8_t)MSL_FTPART_TRANSN) {
        float rot[3], pos[3], scl[3];
        if (local_srt_for_part_f32(t, msid, safe_frame, (uint16_t)MSL_FTPART_TRANSN, rot, pos, scl,
                                   NULL, NULL) == 0) {
          (void)rot;
          (void)scl;
          out_xyz[0] = pos[0];
          out_xyz[1] = pos[1];
          out_xyz[2] = pos[2];
          return 0;
        }
      }
    }
  }

  const float f0 = floorf(safe_frame);
  const float frac = safe_frame - f0;
  const uint16_t frame0 = msl_anim_frame_floor_u16(safe_frame);
  float t0[3];
  if (anim_pose_get_transn(char_id, msid, frame0, t0) != 0) {
    return -1;
  }
  float t1[3] = {t0[0], t0[1], t0[2]};
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0u) {
      (void)anim_pose_get_transn(char_id, msid, frame1, t1);
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  out_xyz[0] = t0[0] + (t1[0] - t0[0]) * a;
  out_xyz[1] = t0[1] + (t1[1] - t0[1]) * a;
  out_xyz[2] = t0[2] + (t1[2] - t0[2]) * a;
  return 0;
}
