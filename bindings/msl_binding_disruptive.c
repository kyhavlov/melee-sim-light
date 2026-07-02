#include "msl_binding_disruptive.h"
#include "msl_binding_internal.h"

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

PyObject* msl_disruptive_scan(PyObject* self, PyObject* args) {
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
        memcpy(seed_bytes + (size_t)lane * sizeof(MslSeed),
               samples_u8 + (size_t)j * sample_stride + seed_off, sizeof(MslSeed));
        memcpy(prev_input_bytes + (size_t)lane * sizeof(MslInput),
               samples_u8 + (size_t)j * sample_stride + prev_input_off, sizeof(MslInput));
        memcpy(input_bytes + (size_t)lane * sizeof(MslInput),
               samples_u8 + (size_t)j * sample_stride + input_off, sizeof(MslInput));
      }
      msl_disr_fill_inactive(seed_bytes, active_count, batch_size, sizeof(MslSeed),
                             sizeof(MslSeed));
      msl_disr_fill_inactive(prev_input_bytes, active_count, batch_size, sizeof(MslInput),
                             sizeof(MslInput));
      msl_disr_fill_inactive(input_bytes, active_count, batch_size, sizeof(MslInput),
                             sizeof(MslInput));
      if (msl_batch_step_input_replay_frame_rng(batch, seed_bytes, sizeof(MslSeed),
                                                prev_input_bytes, sizeof(MslInput), input_bytes,
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
