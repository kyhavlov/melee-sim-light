#include "msl_binding_validation.h"
#include "msl_binding_internal.h"

static uint32_t msl_read_be_u32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

PyObject* msl_slpz_unorder_events(PyObject* self, PyObject* args) {
  (void)self;
  Py_buffer data = {0};
  PyObject* sizes_obj = NULL;
  if (!PyArg_ParseTuple(args, "y*O", &data, &sizes_obj)) {
    return NULL;
  }
  if (!PyList_Check(sizes_obj) || PyList_GET_SIZE(sizes_obj) < 256) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "sizes must be a list with at least 256 entries");
    return NULL;
  }
  if (data.len < 4) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "truncated reordered event stream");
    return NULL;
  }

  int sizes[256];
  for (int i = 0; i < 256; i++) {
    const long v = PyLong_AsLong(PyList_GET_ITEM(sizes_obj, i));
    if (PyErr_Occurred()) {
      PyBuffer_Release(&data);
      return NULL;
    }
    if (v < 0 || v > 65535) {
      PyBuffer_Release(&data);
      PyErr_SetString(PyExc_ValueError, "invalid event payload size");
      return NULL;
    }
    sizes[i] = (int)v;
  }

  const uint8_t* in = (const uint8_t*)data.buf;
  const size_t data_len = (size_t)data.len;
  const uint32_t total_events_u32 = msl_read_be_u32(in);
  const size_t total_events = (size_t)total_events_u32;
  const size_t event_order_offset = 4u;
  if (total_events > SIZE_MAX - event_order_offset) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "invalid reordered event count");
    return NULL;
  }
  const size_t reordered_offset = event_order_offset + total_events;
  if (data_len < reordered_offset) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "truncated reordered event order");
    return NULL;
  }

  uint32_t counts[256] = {0};
  const uint8_t* event_order = in + event_order_offset;
  for (size_t i = 0; i < total_events; i++) {
    counts[event_order[i]]++;
  }

  size_t offsets[256];
  size_t expected_payload_size = 0;
  for (int command = 0; command < 256; command++) {
    offsets[command] = expected_payload_size;
    const size_t size = (size_t)sizes[command];
    const size_t count = (size_t)counts[command];
    if (size != 0u && count > (SIZE_MAX - expected_payload_size) / size) {
      PyBuffer_Release(&data);
      PyErr_SetString(PyExc_ValueError, "invalid reordered event payload size");
      return NULL;
    }
    expected_payload_size += size * count;
  }
  if (data_len - reordered_offset != expected_payload_size) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "invalid reordered event payload size");
    return NULL;
  }
  if (total_events > SIZE_MAX - expected_payload_size ||
      total_events + expected_payload_size > (size_t)PY_SSIZE_T_MAX) {
    PyBuffer_Release(&data);
    PyErr_SetString(PyExc_ValueError, "invalid uncompressed event size");
    return NULL;
  }

  const uint8_t* payloads = in + reordered_offset;
  const size_t out_size = total_events + expected_payload_size;
  PyObject* out_obj = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)out_size);
  if (out_obj == NULL) {
    PyBuffer_Release(&data);
    return NULL;
  }
  uint8_t* out = (uint8_t*)PyBytes_AS_STRING(out_obj);
  uint32_t written[256] = {0};
  size_t out_i = 0;
  for (size_t event_i = 0; event_i < total_events; event_i++) {
    const uint8_t command = event_order[event_i];
    const size_t size = (size_t)sizes[command];
    const size_t stride = (size_t)counts[command];
    const size_t read_start = offsets[command] + (size_t)written[command];
    out[out_i] = command;
    for (size_t j = 0; j < size; j++) {
      out[out_i + 1u + j] = payloads[read_start + j * stride];
    }
    written[command]++;
    out_i += 1u + size;
  }

  PyBuffer_Release(&data);
  return out_obj;
}

static int msl_standard_rollout_compare_code(const MslCompare* out, const MslCompare* ref,
                                             const uint8_t* players, npy_intp player_count,
                                             int profile_rl1) {
  int code = 0;
  int ignored = 0;
  for (npy_intp pi = 0; pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p >= MSL_MAX_PLAYERS) {
      continue;
    }
    if (out->action_id[p] != ref->action_id[p]) {
      code = 1;
      break;
    }
  }
  for (npy_intp pi = 0; code == 0 && pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p < MSL_MAX_PLAYERS && out->animation_index[p] != ref->animation_index[p]) {
      code = 2;
    }
  }
  for (npy_intp pi = 0; code == 0 && pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p < MSL_MAX_PLAYERS && out->on_ground[p] != ref->on_ground[p]) {
      code = 3;
    }
  }
  for (npy_intp pi = 0; code == 0 && pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p < MSL_MAX_PLAYERS && out->hitlag[p] != ref->hitlag[p]) {
      code = 4;
    }
  }
  for (npy_intp pi = 0; code == 0 && pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p < MSL_MAX_PLAYERS && out->hitstun[p] != ref->hitstun[p]) {
      code = 5;
    }
  }
  for (npy_intp pi = 0; pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p >= MSL_MAX_PLAYERS) {
      continue;
    }
    for (int sub = 0; code == 0 && sub < 4; sub++) {
      if (out->state_flags[p][sub] != ref->state_flags[p][sub]) {
        code = 6;
      }
    }
    if (profile_rl1 != 0) {
      const uint8_t diff4 = (uint8_t)(out->state_flags[p][4] ^ ref->state_flags[p][4]);
      if (code == 0 && (diff4 & 0x7Fu) != 0u) {
        code = 6;
      }
      if ((diff4 & 0x80u) != 0u) {
        ignored = 1;
      }
    } else if (code == 0 && out->state_flags[p][4] != ref->state_flags[p][4]) {
      code = 6;
    }
  }
  return code | (ignored ? 0x100 : 0);
}

PyObject* msl_standard_rollout_compare(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* out_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* players_obj = NULL;
  int profile_rl1 = 0;
  if (!PyArg_ParseTuple(args, "OOOi", &out_obj, &ref_obj, &players_obj, &profile_rl1)) {
    return NULL;
  }
  PyArrayObject* out_arr = require_contiguous_array_readonly(out_obj, NPY_UINT8, 2, "out_compare");
  PyArrayObject* ref_arr = require_contiguous_array_readonly(ref_obj, NPY_UINT8, 2, "ref_compare");
  PyArrayObject* players_arr =
      require_contiguous_array_readonly(players_obj, NPY_UINT8, 1, "players");
  if (out_arr == NULL || ref_arr == NULL || players_arr == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out_arr, 0) < 1 || PyArray_DIM(ref_arr, 0) < 1 ||
      PyArray_DIM(out_arr, 1) < (npy_intp)sizeof(MslCompare) ||
      PyArray_DIM(ref_arr, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "compare buffers must contain at least one MslCompare row");
    return NULL;
  }
  const MslCompare* out = (const MslCompare*)(const void*)PyArray_DATA(out_arr);
  const MslCompare* ref = (const MslCompare*)(const void*)PyArray_DATA(ref_arr);
  const uint8_t* players = (const uint8_t*)PyArray_DATA(players_arr);
  const npy_intp player_count = PyArray_DIM(players_arr, 0);
  return PyLong_FromLong(
      (long)msl_standard_rollout_compare_code(out, ref, players, player_count, profile_rl1));
}

static int msl_standard_rollout_reseed_at(MslBatch* batch, const uint8_t* samples_u8,
                                          size_t sample_stride, int record) {
  const uint8_t* seed = samples_u8 + (size_t)record * sample_stride + offsetof(MslSample, seed_t);
  return msl_batch_reseed_seed_rollout(batch, seed, sizeof(MslSeed));
}

static int msl_standard_rollout_step_compare(MslBatch* batch, const uint8_t* samples_u8,
                                             size_t sample_stride, int record,
                                             const uint8_t* players, npy_intp player_count,
                                             int profile_rl1, MslCompare* out) {
  const uint8_t* row = samples_u8 + (size_t)record * sample_stride;
  const uint8_t* seed = row + offsetof(MslSample, seed_t);
  const uint8_t* prev_input = row + offsetof(MslSample, prev_input_t);
  const uint8_t* input = row + offsetof(MslSample, input_t);
  const MslCompare* ref = (const MslCompare*)(const void*)(row + offsetof(MslSample, ref_t1));
  int err = msl_batch_step_input_replay_frame_rng(batch, seed, sizeof(MslSeed), prev_input,
                                                  sizeof(MslInput), input, sizeof(MslInput));
  if (err != 0) {
    return -1;
  }
  err = msl_batch_write_compare(batch, (uint8_t*)out, sizeof(MslCompare));
  if (err != 0) {
    return -2;
  }
  return msl_standard_rollout_compare_code(out, ref, players, player_count, profile_rl1);
}

static int msl_standard_rollout_reseed_at_buffers(MslBatch* batch, const uint8_t* seed_u8,
                                                  size_t seed_stride, int record) {
  const uint8_t* seed = seed_u8 + (size_t)record * seed_stride;
  return msl_batch_reseed_seed_rollout(batch, seed, seed_stride);
}

static int msl_standard_rollout_step_compare_buffers(MslBatch* batch, const uint8_t* seed_u8,
                                                     size_t seed_stride, const uint8_t* prev_u8,
                                                     size_t prev_stride, const uint8_t* input_u8,
                                                     size_t input_stride, const uint8_t* ref_u8,
                                                     size_t ref_stride, int record,
                                                     const uint8_t* players, npy_intp player_count,
                                                     int profile_rl1, MslCompare* out) {
  const uint8_t* seed = seed_u8 + (size_t)record * seed_stride;
  const uint8_t* prev_input = prev_u8 + (size_t)record * prev_stride;
  const uint8_t* input = input_u8 + (size_t)record * input_stride;
  const MslCompare* ref = (const MslCompare*)(const void*)(ref_u8 + (size_t)record * ref_stride);
  int err = msl_batch_step_input_replay_frame_rng(batch, seed, seed_stride, prev_input, prev_stride,
                                                  input, input_stride);
  if (err != 0) {
    return -1;
  }
  err = msl_batch_write_compare(batch, (uint8_t*)out, sizeof(MslCompare));
  if (err != 0) {
    return -2;
  }
  return msl_standard_rollout_compare_code(out, ref, players, player_count, profile_rl1);
}

static int msl_put_bool(PyObject* dict, const char* key, int value);
static int msl_put_string(PyObject* dict, const char* key, const char* value);
static int msl_put_long(PyObject* dict, const char* key, long value);

static int msl_standard_rollout_append_first_row(PyObject* rows, int record, const MslSeed* seed,
                                                 const MslCompare* out, const MslCompare* ref,
                                                 const uint8_t* players, npy_intp player_count,
                                                 int profile_rl1, int seeded_break,
                                                 int streak_start_record, int streak_len) {
  const char* field = NULL;
  int player = -1;
  int subindex = -1;
  long seed_value = 0;
  long out_value = 0;
  long ref_value = 0;

#define MSL_FIRST_ROW_SET_PLAYER_FIELD(FIELD_NAME, FIELD, CAST_TYPE) \
  do {                                                               \
    for (npy_intp pi = 0; pi < player_count; pi++) {                 \
      const uint8_t p = players[pi];                                 \
      if (p < MSL_MAX_PLAYERS && out->FIELD[p] != ref->FIELD[p]) {   \
        field = FIELD_NAME;                                          \
        player = (int)p;                                             \
        seed_value = (long)((CAST_TYPE)seed->FIELD[p]);              \
        out_value = (long)((CAST_TYPE)out->FIELD[p]);                \
        ref_value = (long)((CAST_TYPE)ref->FIELD[p]);                \
        goto found;                                                  \
      }                                                              \
    }                                                                \
  } while (0)

  MSL_FIRST_ROW_SET_PLAYER_FIELD("action_id", action_id, uint16_t);
  MSL_FIRST_ROW_SET_PLAYER_FIELD("animation_index", animation_index, uint32_t);
  MSL_FIRST_ROW_SET_PLAYER_FIELD("on_ground", on_ground, uint8_t);
  MSL_FIRST_ROW_SET_PLAYER_FIELD("hitlag", hitlag, uint16_t);
  MSL_FIRST_ROW_SET_PLAYER_FIELD("hitstun", hitstun, uint16_t);
#undef MSL_FIRST_ROW_SET_PLAYER_FIELD

  for (npy_intp pi = 0; pi < player_count; pi++) {
    const uint8_t p = players[pi];
    if (p >= MSL_MAX_PLAYERS) {
      continue;
    }
    for (int sub = 0; sub < 4; sub++) {
      if (out->state_flags[p][sub] != ref->state_flags[p][sub]) {
        field = "state_flags";
        player = (int)p;
        subindex = sub;
        seed_value = (long)seed->state_flags[p][sub];
        out_value = (long)out->state_flags[p][sub];
        ref_value = (long)ref->state_flags[p][sub];
        goto found;
      }
    }
    if (profile_rl1 != 0) {
      const uint8_t out_cmp = (uint8_t)(out->state_flags[p][4] & 0x7Fu);
      const uint8_t ref_cmp = (uint8_t)(ref->state_flags[p][4] & 0x7Fu);
      if (out_cmp != ref_cmp) {
        field = "state_flags";
        player = (int)p;
        subindex = 4;
        seed_value = (long)seed->state_flags[p][4];
        out_value = (long)out_cmp;
        ref_value = (long)ref_cmp;
        goto found;
      }
    } else if (out->state_flags[p][4] != ref->state_flags[p][4]) {
      field = "state_flags";
      player = (int)p;
      subindex = 4;
      seed_value = (long)seed->state_flags[p][4];
      out_value = (long)out->state_flags[p][4];
      ref_value = (long)ref->state_flags[p][4];
      goto found;
    }
  }

found:
  if (field == NULL) {
    return 0;
  }

  PyObject* row = PyDict_New();
  if (row == NULL || msl_put_long(row, "record", record) != 0 ||
      msl_put_long(row, "seed_frame", seed->frame_id) != 0 ||
      msl_put_long(row, "ref_frame", ref->frame_id) != 0 ||
      msl_put_long(row, "player", player) != 0 || msl_put_string(row, "field", field) != 0 ||
      msl_put_long(row, "subindex", subindex) != 0 || msl_put_long(row, "seed", seed_value) != 0 ||
      msl_put_long(row, "out", out_value) != 0 || msl_put_long(row, "ref", ref_value) != 0 ||
      msl_put_long(row, "streak_start_record", streak_start_record) != 0 ||
      msl_put_long(row, "streak_len", streak_len) != 0 ||
      msl_put_bool(row, "seeded_break", seeded_break != 0) != 0) {
    Py_XDECREF(row);
    return -1;
  }
  if (PyList_Append(rows, row) != 0) {
    Py_DECREF(row);
    return -1;
  }
  Py_DECREF(row);
  return 0;
}

enum {
  MSL_RO_FLOAT_POS_X = 0,
  MSL_RO_FLOAT_POS_Y = 1,
  MSL_RO_FLOAT_SPEED_AIR_X_SELF = 2,
  MSL_RO_FLOAT_SPEED_GROUND_X_SELF = 3,
  MSL_RO_FLOAT_SPEED_Y_SELF = 4,
  MSL_RO_FLOAT_SPEED_X_ATTACK = 5,
  MSL_RO_FLOAT_SPEED_Y_ATTACK = 6,
  MSL_RO_FLOAT_PERCENT = 7,
  MSL_RO_FLOAT_SHIELD_HP = 8,
};

typedef struct MslRolloutFloatTopRow {
  int field;
  double abs_err;
  int record;
  int player;
  int seed_frame;
  int ref_frame;
  float seed;
  float out;
  float ref;
  int seed_action_id;
  int out_action_id;
  int ref_action_id;
  int seed_action_frame;
  int out_action_frame;
  int ref_action_frame;
  int attempt_seeded_retry;
  int discrete_state_matches;
  int streak_start_record;
  int streak_len;
} MslRolloutFloatTopRow;

static const char* msl_rollout_float_field_name(int field) {
  switch (field) {
    case MSL_RO_FLOAT_POS_X:
      return "pos_x";
    case MSL_RO_FLOAT_POS_Y:
      return "pos_y";
    case MSL_RO_FLOAT_SPEED_AIR_X_SELF:
      return "speed_air_x_self";
    case MSL_RO_FLOAT_SPEED_GROUND_X_SELF:
      return "speed_ground_x_self";
    case MSL_RO_FLOAT_SPEED_Y_SELF:
      return "speed_y_self";
    case MSL_RO_FLOAT_SPEED_X_ATTACK:
      return "speed_x_attack";
    case MSL_RO_FLOAT_SPEED_Y_ATTACK:
      return "speed_y_attack";
    case MSL_RO_FLOAT_PERCENT:
      return "percent";
    case MSL_RO_FLOAT_SHIELD_HP:
      return "shield_hp";
    default:
      return NULL;
  }
}

static int msl_rollout_float_field_from_name(const char* name) {
  if (strcmp(name, "pos_x") == 0) {
    return MSL_RO_FLOAT_POS_X;
  }
  if (strcmp(name, "pos_y") == 0) {
    return MSL_RO_FLOAT_POS_Y;
  }
  if (strcmp(name, "speed_air_x_self") == 0) {
    return MSL_RO_FLOAT_SPEED_AIR_X_SELF;
  }
  if (strcmp(name, "speed_ground_x_self") == 0) {
    return MSL_RO_FLOAT_SPEED_GROUND_X_SELF;
  }
  if (strcmp(name, "speed_y_self") == 0) {
    return MSL_RO_FLOAT_SPEED_Y_SELF;
  }
  if (strcmp(name, "speed_x_attack") == 0) {
    return MSL_RO_FLOAT_SPEED_X_ATTACK;
  }
  if (strcmp(name, "speed_y_attack") == 0) {
    return MSL_RO_FLOAT_SPEED_Y_ATTACK;
  }
  if (strcmp(name, "percent") == 0) {
    return MSL_RO_FLOAT_PERCENT;
  }
  if (strcmp(name, "shield_hp") == 0) {
    return MSL_RO_FLOAT_SHIELD_HP;
  }
  return -1;
}

static float msl_rollout_seed_float(const MslSeed* seed, int field, int p) {
  switch (field) {
    case MSL_RO_FLOAT_POS_X:
      return seed->pos_x[p];
    case MSL_RO_FLOAT_POS_Y:
      return seed->pos_y[p];
    case MSL_RO_FLOAT_SPEED_AIR_X_SELF:
      return seed->speed_air_x_self[p];
    case MSL_RO_FLOAT_SPEED_GROUND_X_SELF:
      return seed->speed_ground_x_self[p];
    case MSL_RO_FLOAT_SPEED_Y_SELF:
      return seed->speed_y_self[p];
    case MSL_RO_FLOAT_SPEED_X_ATTACK:
      return seed->speed_x_attack[p];
    case MSL_RO_FLOAT_SPEED_Y_ATTACK:
      return seed->speed_y_attack[p];
    case MSL_RO_FLOAT_PERCENT:
      return seed->percent[p];
    case MSL_RO_FLOAT_SHIELD_HP:
      return seed->shield_hp[p];
    default:
      return 0.0f;
  }
}

static float msl_rollout_compare_float(const MslCompare* cmp, int field, int p) {
  switch (field) {
    case MSL_RO_FLOAT_POS_X:
      return cmp->pos_x[p];
    case MSL_RO_FLOAT_POS_Y:
      return cmp->pos_y[p];
    case MSL_RO_FLOAT_SPEED_AIR_X_SELF:
      return cmp->speed_air_x_self[p];
    case MSL_RO_FLOAT_SPEED_GROUND_X_SELF:
      return cmp->speed_ground_x_self[p];
    case MSL_RO_FLOAT_SPEED_Y_SELF:
      return cmp->speed_y_self[p];
    case MSL_RO_FLOAT_SPEED_X_ATTACK:
      return cmp->speed_x_attack[p];
    case MSL_RO_FLOAT_SPEED_Y_ATTACK:
      return cmp->speed_y_attack[p];
    case MSL_RO_FLOAT_PERCENT:
      return cmp->percent[p];
    case MSL_RO_FLOAT_SHIELD_HP:
      return cmp->shield_hp[p];
    default:
      return 0.0f;
  }
}

static int msl_rollout_float_heap_less(const MslRolloutFloatTopRow* a,
                                       const MslRolloutFloatTopRow* b) {
  if (a->abs_err != b->abs_err) {
    return a->abs_err < b->abs_err;
  }
  if (a->record != b->record) {
    return a->record < b->record;
  }
  if (a->player != b->player) {
    return a->player < b->player;
  }
  if (a->attempt_seeded_retry != b->attempt_seeded_retry) {
    return a->attempt_seeded_retry < b->attempt_seeded_retry;
  }
  if (a->seed != b->seed) {
    return a->seed < b->seed;
  }
  if (a->out != b->out) {
    return a->out < b->out;
  }
  return a->ref < b->ref;
}

static void msl_rollout_float_heap_sift_up(MslRolloutFloatTopRow* rows, int idx) {
  while (idx > 0) {
    const int parent = (idx - 1) / 2;
    if (!msl_rollout_float_heap_less(&rows[idx], &rows[parent])) {
      break;
    }
    const MslRolloutFloatTopRow tmp = rows[parent];
    rows[parent] = rows[idx];
    rows[idx] = tmp;
    idx = parent;
  }
}

static void msl_rollout_float_heap_sift_down(MslRolloutFloatTopRow* rows, int count) {
  int idx = 0;
  for (;;) {
    const int left = idx * 2 + 1;
    const int right = left + 1;
    int best = idx;
    if (left < count && msl_rollout_float_heap_less(&rows[left], &rows[best])) {
      best = left;
    }
    if (right < count && msl_rollout_float_heap_less(&rows[right], &rows[best])) {
      best = right;
    }
    if (best == idx) {
      break;
    }
    const MslRolloutFloatTopRow tmp = rows[best];
    rows[best] = rows[idx];
    rows[idx] = tmp;
    idx = best;
  }
}

static void msl_rollout_float_stable_sort_report_order(MslRolloutFloatTopRow* rows, int count) {
  for (int i = 1; i < count; i++) {
    const MslRolloutFloatTopRow cur = rows[i];
    int j = i - 1;
    while (j >= 0) {
      const MslRolloutFloatTopRow* prev = &rows[j];
      const int cur_before_prev = (cur.abs_err > prev->abs_err) ||
                                  (cur.abs_err == prev->abs_err &&
                                   (cur.record < prev->record ||
                                    (cur.record == prev->record && cur.player < prev->player)));
      if (!cur_before_prev) {
        break;
      }
      rows[j + 1] = rows[j];
      j--;
    }
    rows[j + 1] = cur;
  }
}

static void msl_rollout_float_collect(MslRolloutFloatTopRow* rows, int* counts, const int* fields,
                                      int field_count, int top, double threshold,
                                      const uint8_t* players, npy_intp player_count,
                                      const MslSeed* seed, const MslCompare* out,
                                      const MslCompare* ref, int record, int attempt_seeded_retry,
                                      int discrete_state_matches, int streak_start_record,
                                      int streak_len) {
  if (rows == NULL || counts == NULL || top <= 0 || field_count <= 0) {
    return;
  }
  for (int fi = 0; fi < field_count; fi++) {
    MslRolloutFloatTopRow* field_rows = rows + (size_t)fi * (size_t)top;
    int* count = &counts[fi];
    const int field = fields[fi];
    for (npy_intp pi = 0; pi < player_count; pi++) {
      const int p = (int)players[pi];
      const float out_v = msl_rollout_compare_float(out, field, p);
      const float ref_v = msl_rollout_compare_float(ref, field, p);
      const double abs_err = fabs((double)out_v - (double)ref_v);
      if (abs_err < threshold || abs_err <= 0.0) {
        continue;
      }
      MslRolloutFloatTopRow next = (MslRolloutFloatTopRow){
          .field = field,
          .abs_err = abs_err,
          .record = record,
          .player = p,
          .seed_frame = seed->frame_id,
          .ref_frame = ref->frame_id,
          .seed = msl_rollout_seed_float(seed, field, p),
          .out = out_v,
          .ref = ref_v,
          .seed_action_id = seed->action_id[p],
          .out_action_id = out->action_id[p],
          .ref_action_id = ref->action_id[p],
          .seed_action_frame = seed->action_frame[p],
          .out_action_frame = out->action_frame[p],
          .ref_action_frame = ref->action_frame[p],
          .attempt_seeded_retry = attempt_seeded_retry,
          .discrete_state_matches = discrete_state_matches,
          .streak_start_record = streak_start_record,
          .streak_len = streak_len,
      };
      int dst = *count;
      if (dst < top) {
        field_rows[dst] = next;
        *count = dst + 1;
        msl_rollout_float_heap_sift_up(field_rows, dst);
      } else if (abs_err > field_rows[0].abs_err) {
        field_rows[0] = next;
        msl_rollout_float_heap_sift_down(field_rows, top);
      }
    }
  }
}

static int msl_put_bool(PyObject* dict, const char* key, int value) {
  PyObject* obj = value ? Py_True : Py_False;
  Py_INCREF(obj);
  const int err = PyDict_SetItemString(dict, key, obj);
  Py_DECREF(obj);
  return err;
}

static int msl_put_double(PyObject* dict, const char* key, double value) {
  PyObject* obj = PyFloat_FromDouble(value);
  if (obj == NULL) {
    return -1;
  }
  const int err = PyDict_SetItemString(dict, key, obj);
  Py_DECREF(obj);
  return err;
}

static int msl_put_string(PyObject* dict, const char* key, const char* value) {
  PyObject* obj = PyUnicode_FromString(value);
  if (obj == NULL) {
    return -1;
  }
  const int err = PyDict_SetItemString(dict, key, obj);
  Py_DECREF(obj);
  return err;
}

static int msl_put_long(PyObject* dict, const char* key, long value) {
  PyObject* obj = PyLong_FromLong(value);
  if (obj == NULL) {
    return -1;
  }
  const int err = PyDict_SetItemString(dict, key, obj);
  Py_DECREF(obj);
  return err;
}

enum {
  MSL_OS_ACTION_ID = 0,
  MSL_OS_ACTION_FRAME,
  MSL_OS_ON_GROUND,
  MSL_OS_FACING,
  MSL_OS_STOCKS,
  MSL_OS_JUMPS_LEFT,
  MSL_OS_IS_DEAD,
  MSL_OS_HITLAG,
  MSL_OS_HITSTUN,
  MSL_OS_L_CANCEL,
  MSL_OS_HURTBOX_STATE,
  MSL_OS_GROUND_ID,
  MSL_OS_ANIMATION_INDEX,
  MSL_OS_INSTANCE_HIT_BY,
  MSL_OS_INSTANCE_ID,
  MSL_OS_LAST_ATTACK_LANDED,
  MSL_OS_COMBO_COUNT,
  MSL_OS_LAST_HIT_BY,
  MSL_OS_STATE_FLAGS,
  MSL_OS_ITEM_EXISTS,
  MSL_OS_ITEM_TYPE,
  MSL_OS_ITEM_STATE,
  MSL_OS_ITEM_OWNER,
  MSL_OS_ITEM_INSTANCE_ID,
  MSL_OS_FIELD_COUNT,
};

enum {
  MSL_OS_FLOAT_POS_X = 0,
  MSL_OS_FLOAT_POS_Y,
  MSL_OS_FLOAT_SPEED_AIR_X_SELF,
  MSL_OS_FLOAT_SPEED_GROUND_X_SELF,
  MSL_OS_FLOAT_SPEED_Y_SELF,
  MSL_OS_FLOAT_SPEED_X_ATTACK,
  MSL_OS_FLOAT_SPEED_Y_ATTACK,
  MSL_OS_FLOAT_PERCENT,
  MSL_OS_FLOAT_SHIELD_HP,
  MSL_OS_FLOAT_ITEM_POS_X,
  MSL_OS_FLOAT_ITEM_POS_Y,
  MSL_OS_FLOAT_ITEM_VEL_X,
  MSL_OS_FLOAT_ITEM_VEL_Y,
  MSL_OS_FLOAT_COUNT,
};

typedef struct MslOneStepSummary {
  int num_players;
  int profile_rl1;
  int player_float_capacity;
  int item_float_capacity;
  long mismatches[MSL_OS_FIELD_COUNT];
  long strict_mismatches[MSL_OS_FIELD_COUNT];
  long ignored_state_flags_4_0x80;
  int float_counts[MSL_OS_FLOAT_COUNT];
  float* float_err_abs[MSL_OS_FLOAT_COUNT];
  float* float_ref_abs[MSL_OS_FLOAT_COUNT];
} MslOneStepSummary;

static const char* msl_one_step_float_field_name(int field) {
  static const char* names[MSL_OS_FLOAT_COUNT] = {
      "pos_x",          "pos_y",          "speed_air_x_self", "speed_ground_x_self", "speed_y_self",
      "speed_x_attack", "speed_y_attack", "percent",          "shield_hp",           "item_pos_x",
      "item_pos_y",     "item_vel_x",     "item_vel_y",
  };
  return (field >= 0 && field < MSL_OS_FLOAT_COUNT) ? names[field] : NULL;
}

static void msl_one_step_summary_free(MslOneStepSummary* s) {
  if (s == NULL) {
    return;
  }
  for (int i = 0; i < MSL_OS_FLOAT_COUNT; i++) {
    PyMem_Free(s->float_err_abs[i]);
    PyMem_Free(s->float_ref_abs[i]);
  }
  PyMem_Free(s);
}

static void msl_one_step_summary_capsule_destructor(PyObject* capsule) {
  MslOneStepSummary* s = (MslOneStepSummary*)PyCapsule_GetPointer(capsule, "msl.OneStepSummary");
  if (s == NULL) {
    return;
  }
  msl_one_step_summary_free(s);
}

static MslOneStepSummary* msl_one_step_summary_unpack(PyObject* obj) {
  MslOneStepSummary* s = (MslOneStepSummary*)PyCapsule_GetPointer(obj, "msl.OneStepSummary");
  if (s == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid one-step summary handle");
    return NULL;
  }
  return s;
}

static int msl_one_step_float_capacity(const MslOneStepSummary* s, int field) {
  return field < MSL_OS_FLOAT_ITEM_POS_X ? s->player_float_capacity : s->item_float_capacity;
}

static int msl_one_step_append_float(MslOneStepSummary* s, int field, float out_v, float ref_v) {
  const int idx = s->float_counts[field];
  if (idx >= msl_one_step_float_capacity(s, field)) {
    PyErr_SetString(PyExc_ValueError, "one-step summary float capacity exceeded");
    return -1;
  }
  const float err = out_v - ref_v;
  s->float_err_abs[field][idx] = fabsf(err);
  s->float_ref_abs[field][idx] = fabsf(ref_v);
  s->float_counts[field] = idx + 1;
  return 0;
}

PyObject* msl_one_step_summary_create(PyObject* self, PyObject* args) {
  (void)self;
  int total_records = 0;
  int num_players = 0;
  int profile_rl1 = 0;
  if (!PyArg_ParseTuple(args, "iii", &total_records, &num_players, &profile_rl1)) {
    return NULL;
  }
  if (total_records < 0) {
    PyErr_SetString(PyExc_ValueError, "total_records must be non-negative");
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  MslOneStepSummary* s = (MslOneStepSummary*)PyMem_Calloc(1u, sizeof(*s));
  if (s == NULL) {
    PyErr_NoMemory();
    return NULL;
  }
  s->num_players = num_players;
  s->profile_rl1 = profile_rl1 != 0;
  s->player_float_capacity = total_records * num_players;
  s->item_float_capacity = total_records * MSL_MAX_ITEMS;
  for (int i = 0; i < MSL_OS_FLOAT_COUNT; i++) {
    const int capacity = msl_one_step_float_capacity(s, i);
    if (capacity <= 0) {
      continue;
    }
    s->float_err_abs[i] = (float*)PyMem_Malloc((size_t)capacity * sizeof(float));
    s->float_ref_abs[i] = (float*)PyMem_Malloc((size_t)capacity * sizeof(float));
    if (s->float_err_abs[i] == NULL || s->float_ref_abs[i] == NULL) {
      msl_one_step_summary_free(s);
      PyErr_NoMemory();
      return NULL;
    }
  }
  PyObject* capsule =
      PyCapsule_New(s, "msl.OneStepSummary", msl_one_step_summary_capsule_destructor);
  if (capsule == NULL) {
    msl_one_step_summary_free(s);
    return NULL;
  }
  return capsule;
}

static int msl_one_step_summary_accumulate_row(MslOneStepSummary* s, const MslCompare* out,
                                               const MslCompare* ref) {
  for (int p = 0; p < s->num_players; p++) {
    if (out->action_id[p] != ref->action_id[p]) {
      s->mismatches[MSL_OS_ACTION_ID]++;
      s->strict_mismatches[MSL_OS_ACTION_ID]++;
    }
    if (out->action_frame[p] != ref->action_frame[p]) {
      s->mismatches[MSL_OS_ACTION_FRAME]++;
      s->strict_mismatches[MSL_OS_ACTION_FRAME]++;
    }
    if (out->on_ground[p] != ref->on_ground[p]) {
      s->mismatches[MSL_OS_ON_GROUND]++;
      s->strict_mismatches[MSL_OS_ON_GROUND]++;
    }
    if (out->facing[p] != ref->facing[p]) {
      s->mismatches[MSL_OS_FACING]++;
      s->strict_mismatches[MSL_OS_FACING]++;
    }
    if (out->stocks[p] != ref->stocks[p]) {
      s->mismatches[MSL_OS_STOCKS]++;
      s->strict_mismatches[MSL_OS_STOCKS]++;
    }
    if (out->jumps_left[p] != ref->jumps_left[p]) {
      s->mismatches[MSL_OS_JUMPS_LEFT]++;
      s->strict_mismatches[MSL_OS_JUMPS_LEFT]++;
    }
    if (out->is_dead[p] != ref->is_dead[p]) {
      s->mismatches[MSL_OS_IS_DEAD]++;
      s->strict_mismatches[MSL_OS_IS_DEAD]++;
    }
    if (out->hitlag[p] != ref->hitlag[p]) {
      s->mismatches[MSL_OS_HITLAG]++;
      s->strict_mismatches[MSL_OS_HITLAG]++;
    }
    if (out->hitstun[p] != ref->hitstun[p]) {
      s->mismatches[MSL_OS_HITSTUN]++;
      s->strict_mismatches[MSL_OS_HITSTUN]++;
    }
    if (out->l_cancel[p] != ref->l_cancel[p]) {
      s->mismatches[MSL_OS_L_CANCEL]++;
      s->strict_mismatches[MSL_OS_L_CANCEL]++;
    }
    if (out->hurtbox_state[p] != ref->hurtbox_state[p]) {
      s->mismatches[MSL_OS_HURTBOX_STATE]++;
      s->strict_mismatches[MSL_OS_HURTBOX_STATE]++;
    }
    if (out->ground_id[p] != ref->ground_id[p]) {
      s->mismatches[MSL_OS_GROUND_ID]++;
      s->strict_mismatches[MSL_OS_GROUND_ID]++;
    }
    if (out->animation_index[p] != ref->animation_index[p]) {
      s->mismatches[MSL_OS_ANIMATION_INDEX]++;
      s->strict_mismatches[MSL_OS_ANIMATION_INDEX]++;
    }
    if (out->instance_hit_by[p] != ref->instance_hit_by[p]) {
      s->mismatches[MSL_OS_INSTANCE_HIT_BY]++;
      s->strict_mismatches[MSL_OS_INSTANCE_HIT_BY]++;
    }
    if (out->instance_id[p] != ref->instance_id[p]) {
      s->mismatches[MSL_OS_INSTANCE_ID]++;
      s->strict_mismatches[MSL_OS_INSTANCE_ID]++;
    }
    if (out->last_attack_landed[p] != ref->last_attack_landed[p]) {
      s->mismatches[MSL_OS_LAST_ATTACK_LANDED]++;
      s->strict_mismatches[MSL_OS_LAST_ATTACK_LANDED]++;
    }
    if (out->combo_count[p] != ref->combo_count[p]) {
      s->mismatches[MSL_OS_COMBO_COUNT]++;
      s->strict_mismatches[MSL_OS_COMBO_COUNT]++;
    }
    if (out->last_hit_by[p] != ref->last_hit_by[p]) {
      s->mismatches[MSL_OS_LAST_HIT_BY]++;
      s->strict_mismatches[MSL_OS_LAST_HIT_BY]++;
    }
    for (int sub = 0; sub < 5; sub++) {
      const uint8_t xo = (uint8_t)(out->state_flags[p][sub] ^ ref->state_flags[p][sub]);
      if (xo != 0u) {
        s->strict_mismatches[MSL_OS_STATE_FLAGS]++;
      }
      uint8_t scored = xo;
      if (s->profile_rl1 != 0 && sub == 4) {
        if ((xo & 0x80u) != 0u) {
          s->ignored_state_flags_4_0x80++;
        }
        scored = (uint8_t)(scored & 0x7Fu);
      }
      if (scored != 0u) {
        s->mismatches[MSL_OS_STATE_FLAGS]++;
      }
    }
    if (msl_one_step_append_float(s, MSL_OS_FLOAT_POS_X, out->pos_x[p], ref->pos_x[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_POS_Y, out->pos_y[p], ref->pos_y[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_AIR_X_SELF, out->speed_air_x_self[p],
                                  ref->speed_air_x_self[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_GROUND_X_SELF, out->speed_ground_x_self[p],
                                  ref->speed_ground_x_self[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_Y_SELF, out->speed_y_self[p],
                                  ref->speed_y_self[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_X_ATTACK, out->speed_x_attack[p],
                                  ref->speed_x_attack[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_Y_ATTACK, out->speed_y_attack[p],
                                  ref->speed_y_attack[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_PERCENT, out->percent[p], ref->percent[p]) != 0 ||
        msl_one_step_append_float(s, MSL_OS_FLOAT_SHIELD_HP, out->shield_hp[p],
                                  ref->shield_hp[p]) != 0) {
      return -1;
    }
  }
  for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
    const MeleeItem* oi = &out->items[slot];
    const MeleeItem* ri = &ref->items[slot];
    if (oi->exists != ri->exists) {
      s->mismatches[MSL_OS_ITEM_EXISTS]++;
      s->strict_mismatches[MSL_OS_ITEM_EXISTS]++;
    }
    if (oi->type != ri->type) {
      s->mismatches[MSL_OS_ITEM_TYPE]++;
      s->strict_mismatches[MSL_OS_ITEM_TYPE]++;
    }
    if (oi->state != ri->state) {
      s->mismatches[MSL_OS_ITEM_STATE]++;
      s->strict_mismatches[MSL_OS_ITEM_STATE]++;
    }
    if (oi->owner != ri->owner) {
      s->mismatches[MSL_OS_ITEM_OWNER]++;
      s->strict_mismatches[MSL_OS_ITEM_OWNER]++;
    }
    if (oi->instance_id != ri->instance_id) {
      s->mismatches[MSL_OS_ITEM_INSTANCE_ID]++;
      s->strict_mismatches[MSL_OS_ITEM_INSTANCE_ID]++;
    }
    if (ri->exists != 0u) {
      if (msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_POS_X, oi->pos_x, ri->pos_x) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_POS_Y, oi->pos_y, ri->pos_y) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_VEL_X, oi->vel_x, ri->vel_x) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_VEL_Y, oi->vel_y, ri->vel_y) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

PyObject* msl_one_step_summary_accumulate(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* summary_obj = NULL;
  PyObject* out_obj = NULL;
  PyObject* samples_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &summary_obj, &out_obj, &samples_obj)) {
    return NULL;
  }
  MslOneStepSummary* s = msl_one_step_summary_unpack(summary_obj);
  if (s == NULL) {
    return NULL;
  }
  PyArrayObject* out_arr = require_contiguous_array_readonly(out_obj, NPY_UINT8, 2, "out_compare");
  PyArrayObject* samples_arr =
      require_contiguous_array_readonly(samples_obj, NPY_UINT8, 2, "samples_u8");
  if (out_arr == NULL || samples_arr == NULL) {
    return NULL;
  }
  const int n = (int)PyArray_DIM(out_arr, 0);
  if (PyArray_DIM(samples_arr, 0) != (npy_intp)n ||
      PyArray_DIM(out_arr, 1) < (npy_intp)sizeof(MslCompare) ||
      PyArray_DIM(samples_arr, 1) < (npy_intp)sizeof(MslSample)) {
    PyErr_SetString(PyExc_ValueError,
                    "one_step_summary_accumulate expects matching native compare/sample rows");
    return NULL;
  }

  const uint8_t* out_u8 = (const uint8_t*)PyArray_DATA(out_arr);
  const uint8_t* samples_u8 = (const uint8_t*)PyArray_DATA(samples_arr);
  const size_t out_stride = (size_t)PyArray_STRIDE(out_arr, 0);
  const size_t sample_stride = (size_t)PyArray_STRIDE(samples_arr, 0);

  for (int r = 0; r < n; r++) {
    const MslCompare* out = (const MslCompare*)(const void*)(out_u8 + (size_t)r * out_stride);
    const MslSample* sample =
        (const MslSample*)(const void*)(samples_u8 + (size_t)r * sample_stride);
    const MslCompare* ref = &sample->ref_t1;
    for (int p = 0; p < s->num_players; p++) {
      if (out->action_id[p] != ref->action_id[p]) {
        s->mismatches[MSL_OS_ACTION_ID]++;
        s->strict_mismatches[MSL_OS_ACTION_ID]++;
      }
      if (out->action_frame[p] != ref->action_frame[p]) {
        s->mismatches[MSL_OS_ACTION_FRAME]++;
        s->strict_mismatches[MSL_OS_ACTION_FRAME]++;
      }
      if (out->on_ground[p] != ref->on_ground[p]) {
        s->mismatches[MSL_OS_ON_GROUND]++;
        s->strict_mismatches[MSL_OS_ON_GROUND]++;
      }
      if (out->facing[p] != ref->facing[p]) {
        s->mismatches[MSL_OS_FACING]++;
        s->strict_mismatches[MSL_OS_FACING]++;
      }
      if (out->stocks[p] != ref->stocks[p]) {
        s->mismatches[MSL_OS_STOCKS]++;
        s->strict_mismatches[MSL_OS_STOCKS]++;
      }
      if (out->jumps_left[p] != ref->jumps_left[p]) {
        s->mismatches[MSL_OS_JUMPS_LEFT]++;
        s->strict_mismatches[MSL_OS_JUMPS_LEFT]++;
      }
      if (out->is_dead[p] != ref->is_dead[p]) {
        s->mismatches[MSL_OS_IS_DEAD]++;
        s->strict_mismatches[MSL_OS_IS_DEAD]++;
      }
      if (out->hitlag[p] != ref->hitlag[p]) {
        s->mismatches[MSL_OS_HITLAG]++;
        s->strict_mismatches[MSL_OS_HITLAG]++;
      }
      if (out->hitstun[p] != ref->hitstun[p]) {
        s->mismatches[MSL_OS_HITSTUN]++;
        s->strict_mismatches[MSL_OS_HITSTUN]++;
      }
      if (out->l_cancel[p] != ref->l_cancel[p]) {
        s->mismatches[MSL_OS_L_CANCEL]++;
        s->strict_mismatches[MSL_OS_L_CANCEL]++;
      }
      if (out->hurtbox_state[p] != ref->hurtbox_state[p]) {
        s->mismatches[MSL_OS_HURTBOX_STATE]++;
        s->strict_mismatches[MSL_OS_HURTBOX_STATE]++;
      }
      if (out->ground_id[p] != ref->ground_id[p]) {
        s->mismatches[MSL_OS_GROUND_ID]++;
        s->strict_mismatches[MSL_OS_GROUND_ID]++;
      }
      if (out->animation_index[p] != ref->animation_index[p]) {
        s->mismatches[MSL_OS_ANIMATION_INDEX]++;
        s->strict_mismatches[MSL_OS_ANIMATION_INDEX]++;
      }
      if (out->instance_hit_by[p] != ref->instance_hit_by[p]) {
        s->mismatches[MSL_OS_INSTANCE_HIT_BY]++;
        s->strict_mismatches[MSL_OS_INSTANCE_HIT_BY]++;
      }
      if (out->instance_id[p] != ref->instance_id[p]) {
        s->mismatches[MSL_OS_INSTANCE_ID]++;
        s->strict_mismatches[MSL_OS_INSTANCE_ID]++;
      }
      if (out->last_attack_landed[p] != ref->last_attack_landed[p]) {
        s->mismatches[MSL_OS_LAST_ATTACK_LANDED]++;
        s->strict_mismatches[MSL_OS_LAST_ATTACK_LANDED]++;
      }
      if (out->combo_count[p] != ref->combo_count[p]) {
        s->mismatches[MSL_OS_COMBO_COUNT]++;
        s->strict_mismatches[MSL_OS_COMBO_COUNT]++;
      }
      if (out->last_hit_by[p] != ref->last_hit_by[p]) {
        s->mismatches[MSL_OS_LAST_HIT_BY]++;
        s->strict_mismatches[MSL_OS_LAST_HIT_BY]++;
      }
      for (int sub = 0; sub < 5; sub++) {
        const uint8_t xo = (uint8_t)(out->state_flags[p][sub] ^ ref->state_flags[p][sub]);
        if (xo != 0u) {
          s->strict_mismatches[MSL_OS_STATE_FLAGS]++;
        }
        uint8_t scored = xo;
        if (s->profile_rl1 != 0 && sub == 4) {
          if ((xo & 0x80u) != 0u) {
            s->ignored_state_flags_4_0x80++;
          }
          scored = (uint8_t)(scored & 0x7Fu);
        }
        if (scored != 0u) {
          s->mismatches[MSL_OS_STATE_FLAGS]++;
        }
      }
      if (msl_one_step_append_float(s, MSL_OS_FLOAT_POS_X, out->pos_x[p], ref->pos_x[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_POS_Y, out->pos_y[p], ref->pos_y[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_AIR_X_SELF, out->speed_air_x_self[p],
                                    ref->speed_air_x_self[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_GROUND_X_SELF,
                                    out->speed_ground_x_self[p],
                                    ref->speed_ground_x_self[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_Y_SELF, out->speed_y_self[p],
                                    ref->speed_y_self[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_X_ATTACK, out->speed_x_attack[p],
                                    ref->speed_x_attack[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SPEED_Y_ATTACK, out->speed_y_attack[p],
                                    ref->speed_y_attack[p]) != 0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_PERCENT, out->percent[p], ref->percent[p]) !=
              0 ||
          msl_one_step_append_float(s, MSL_OS_FLOAT_SHIELD_HP, out->shield_hp[p],
                                    ref->shield_hp[p]) != 0) {
        return NULL;
      }
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      const MeleeItem* oi = &out->items[slot];
      const MeleeItem* ri = &ref->items[slot];
      if (oi->exists != ri->exists) {
        s->mismatches[MSL_OS_ITEM_EXISTS]++;
        s->strict_mismatches[MSL_OS_ITEM_EXISTS]++;
      }
      if (oi->type != ri->type) {
        s->mismatches[MSL_OS_ITEM_TYPE]++;
        s->strict_mismatches[MSL_OS_ITEM_TYPE]++;
      }
      if (oi->state != ri->state) {
        s->mismatches[MSL_OS_ITEM_STATE]++;
        s->strict_mismatches[MSL_OS_ITEM_STATE]++;
      }
      if (oi->owner != ri->owner) {
        s->mismatches[MSL_OS_ITEM_OWNER]++;
        s->strict_mismatches[MSL_OS_ITEM_OWNER]++;
      }
      if (oi->instance_id != ri->instance_id) {
        s->mismatches[MSL_OS_ITEM_INSTANCE_ID]++;
        s->strict_mismatches[MSL_OS_ITEM_INSTANCE_ID]++;
      }
      if (ri->exists != 0u) {
        if (msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_POS_X, oi->pos_x, ri->pos_x) != 0 ||
            msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_POS_Y, oi->pos_y, ri->pos_y) != 0 ||
            msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_VEL_X, oi->vel_x, ri->vel_x) != 0 ||
            msl_one_step_append_float(s, MSL_OS_FLOAT_ITEM_VEL_Y, oi->vel_y, ri->vel_y) != 0) {
          return NULL;
        }
      }
    }
  }
  Py_RETURN_NONE;
}

static PyObject* msl_one_step_float_array(float* data, int count) {
  npy_intp dims[1] = {(npy_intp)count};
  return PyArray_SimpleNewFromData(1, dims, NPY_FLOAT32, data);
}

static double msl_numpy_scalar_double(PyObject* obj) {
  const double out = PyFloat_AsDouble(obj);
  return out;
}

static PyObject* msl_numpy_call0(PyObject* obj, const char* method) {
  return PyObject_CallMethod(obj, method, NULL);
}

static PyObject* msl_numpy_quantile(PyObject* quantile_func, PyObject* arr) {
  PyObject* q = PyFloat_FromDouble(0.95);
  if (q == NULL) {
    return NULL;
  }
  PyObject* out = PyObject_CallFunctionObjArgs(quantile_func, arr, q, NULL);
  Py_DECREF(q);
  return out;
}

PyObject* msl_one_step_summary_finish(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* summary_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &summary_obj)) {
    return NULL;
  }
  MslOneStepSummary* s = msl_one_step_summary_unpack(summary_obj);
  if (s == NULL) {
    return NULL;
  }

  PyObject* result = PyDict_New();
  PyObject* mismatch_list = PyList_New(MSL_OS_FIELD_COUNT);
  PyObject* strict_list = PyList_New(MSL_OS_FIELD_COUNT);
  PyObject* float_dict = PyDict_New();
  if (result == NULL || mismatch_list == NULL || strict_list == NULL || float_dict == NULL) {
    Py_XDECREF(result);
    Py_XDECREF(mismatch_list);
    Py_XDECREF(strict_list);
    Py_XDECREF(float_dict);
    return NULL;
  }
  for (int i = 0; i < MSL_OS_FIELD_COUNT; i++) {
    PyObject* a = PyLong_FromLong(s->mismatches[i]);
    PyObject* b = PyLong_FromLong(s->strict_mismatches[i]);
    if (a == NULL || b == NULL) {
      Py_XDECREF(a);
      Py_XDECREF(b);
      Py_DECREF(result);
      Py_DECREF(mismatch_list);
      Py_DECREF(strict_list);
      Py_DECREF(float_dict);
      return NULL;
    }
    PyList_SET_ITEM(mismatch_list, i, a);
    PyList_SET_ITEM(strict_list, i, b);
  }

  PyObject* numpy_mod = PyImport_ImportModule("numpy");
  PyObject* quantile_func =
      numpy_mod != NULL ? PyObject_GetAttrString(numpy_mod, "quantile") : NULL;
  Py_XDECREF(numpy_mod);
  if (quantile_func == NULL) {
    Py_DECREF(result);
    Py_DECREF(mismatch_list);
    Py_DECREF(strict_list);
    Py_DECREF(float_dict);
    return NULL;
  }

  double total_norm_sum = 0.0;
  long total_norm_count = 0;
  for (int i = 0; i < MSL_OS_FLOAT_COUNT; i++) {
    const int count = s->float_counts[i];
    PyObject* metrics = PyDict_New();
    if (metrics == NULL) {
      Py_DECREF(result);
      Py_DECREF(mismatch_list);
      Py_DECREF(strict_list);
      Py_DECREF(float_dict);
      Py_DECREF(quantile_func);
      return NULL;
    }
    if (count == 0) {
      if (msl_put_double(metrics, "mae", 0.0) != 0 || msl_put_double(metrics, "p95", 0.0) != 0 ||
          msl_put_double(metrics, "max", 0.0) != 0 || msl_put_long(metrics, "count", 0) != 0) {
        Py_DECREF(metrics);
        Py_DECREF(result);
        Py_DECREF(mismatch_list);
        Py_DECREF(strict_list);
        Py_DECREF(float_dict);
        Py_DECREF(quantile_func);
        return NULL;
      }
    } else {
      PyObject* err_arr = msl_one_step_float_array(s->float_err_abs[i], count);
      PyObject* ref_arr = msl_one_step_float_array(s->float_ref_abs[i], count);
      PyObject* mean_obj = err_arr != NULL ? msl_numpy_call0(err_arr, "mean") : NULL;
      PyObject* p95_obj = err_arr != NULL ? msl_numpy_quantile(quantile_func, err_arr) : NULL;
      PyObject* max_obj = err_arr != NULL ? msl_numpy_call0(err_arr, "max") : NULL;
      PyObject* sum_obj = err_arr != NULL ? msl_numpy_call0(err_arr, "sum") : NULL;
      PyObject* ref_p95_obj = ref_arr != NULL ? msl_numpy_quantile(quantile_func, ref_arr) : NULL;
      if (err_arr == NULL || ref_arr == NULL || mean_obj == NULL || p95_obj == NULL ||
          max_obj == NULL || sum_obj == NULL || ref_p95_obj == NULL) {
        Py_XDECREF(err_arr);
        Py_XDECREF(ref_arr);
        Py_XDECREF(mean_obj);
        Py_XDECREF(p95_obj);
        Py_XDECREF(max_obj);
        Py_XDECREF(sum_obj);
        Py_XDECREF(ref_p95_obj);
        Py_DECREF(metrics);
        Py_DECREF(result);
        Py_DECREF(mismatch_list);
        Py_DECREF(strict_list);
        Py_DECREF(float_dict);
        Py_DECREF(quantile_func);
        return NULL;
      }
      const double mae = msl_numpy_scalar_double(mean_obj);
      const double p95 = msl_numpy_scalar_double(p95_obj);
      const double mx = msl_numpy_scalar_double(max_obj);
      const double sum_abs = msl_numpy_scalar_double(sum_obj);
      double ref_p95 = msl_numpy_scalar_double(ref_p95_obj);
      if (PyErr_Occurred()) {
        Py_DECREF(err_arr);
        Py_DECREF(ref_arr);
        Py_DECREF(mean_obj);
        Py_DECREF(p95_obj);
        Py_DECREF(max_obj);
        Py_DECREF(sum_obj);
        Py_DECREF(ref_p95_obj);
        Py_DECREF(metrics);
        Py_DECREF(result);
        Py_DECREF(mismatch_list);
        Py_DECREF(strict_list);
        Py_DECREF(float_dict);
        Py_DECREF(quantile_func);
        return NULL;
      }
      if (ref_p95 <= 0.0) {
        ref_p95 = 1e-6;
      }
      total_norm_sum += sum_abs / ref_p95;
      total_norm_count += (long)count;
      Py_DECREF(err_arr);
      Py_DECREF(ref_arr);
      Py_DECREF(mean_obj);
      Py_DECREF(p95_obj);
      Py_DECREF(max_obj);
      Py_DECREF(sum_obj);
      Py_DECREF(ref_p95_obj);
      if (msl_put_double(metrics, "mae", mae) != 0 || msl_put_double(metrics, "p95", p95) != 0 ||
          msl_put_double(metrics, "max", mx) != 0 || msl_put_long(metrics, "count", count) != 0) {
        Py_DECREF(metrics);
        Py_DECREF(result);
        Py_DECREF(mismatch_list);
        Py_DECREF(strict_list);
        Py_DECREF(float_dict);
        Py_DECREF(quantile_func);
        return NULL;
      }
    }
    const char* name = msl_one_step_float_field_name(i);
    if (name == NULL || PyDict_SetItemString(float_dict, name, metrics) != 0) {
      Py_DECREF(metrics);
      Py_DECREF(result);
      Py_DECREF(mismatch_list);
      Py_DECREF(strict_list);
      Py_DECREF(float_dict);
      Py_DECREF(quantile_func);
      return NULL;
    }
    Py_DECREF(metrics);
  }
  Py_DECREF(quantile_func);

  if (PyDict_SetItemString(result, "mismatches", mismatch_list) != 0 ||
      PyDict_SetItemString(result, "strict_mismatches", strict_list) != 0 ||
      PyDict_SetItemString(result, "float_metrics", float_dict) != 0 ||
      msl_put_long(result, "ignored_state_flags_4_0x80", s->ignored_state_flags_4_0x80) != 0 ||
      msl_put_double(result, "float_norm_sum", total_norm_sum) != 0 ||
      msl_put_long(result, "float_norm_count", total_norm_count) != 0) {
    Py_DECREF(result);
    Py_DECREF(mismatch_list);
    Py_DECREF(strict_list);
    Py_DECREF(float_dict);
    return NULL;
  }
  Py_DECREF(mismatch_list);
  Py_DECREF(strict_list);
  Py_DECREF(float_dict);
  return result;
}

PyObject* msl_one_step_summary(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* out_obj = NULL;
  PyObject* samples_obj = NULL;
  int num_players = 0;
  int profile_rl1 = 0;
  if (!PyArg_ParseTuple(args, "OOii", &out_obj, &samples_obj, &num_players, &profile_rl1)) {
    return NULL;
  }
  PyArrayObject* out_arr = require_contiguous_array_readonly(out_obj, NPY_UINT8, 2, "out_compare");
  if (out_arr == NULL) {
    return NULL;
  }
  PyObject* create_args =
      Py_BuildValue("iii", (int)PyArray_DIM(out_arr, 0), num_players, profile_rl1);
  if (create_args == NULL) {
    return NULL;
  }
  PyObject* capsule = msl_one_step_summary_create(NULL, create_args);
  Py_DECREF(create_args);
  if (capsule == NULL) {
    return NULL;
  }
  PyObject* accum_args = Py_BuildValue("OOO", capsule, out_obj, samples_obj);
  if (accum_args == NULL) {
    Py_DECREF(capsule);
    return NULL;
  }
  PyObject* accum = msl_one_step_summary_accumulate(NULL, accum_args);
  Py_DECREF(accum_args);
  if (accum == NULL) {
    Py_DECREF(capsule);
    return NULL;
  }
  Py_DECREF(accum);
  PyObject* finish_args = Py_BuildValue("O", capsule);
  if (finish_args == NULL) {
    Py_DECREF(capsule);
    return NULL;
  }
  PyObject* result = msl_one_step_summary_finish(NULL, finish_args);
  Py_DECREF(finish_args);
  Py_DECREF(capsule);
  return result;
}

PyObject* msl_one_step_eval_samples(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* samples_obj = NULL;
  int num_players = 0;
  int profile_rl1 = 0;
  if (!PyArg_ParseTuple(args, "OOii", &handle_obj, &samples_obj, &num_players, &profile_rl1)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  PyArrayObject* samples_arr =
      require_contiguous_array_readonly(samples_obj, NPY_UINT8, 2, "samples_u8");
  if (samples_arr == NULL) {
    return NULL;
  }
  const int total_records = (int)PyArray_DIM(samples_arr, 0);
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  if (PyArray_DIM(samples_arr, 1) < (npy_intp)sizeof(MslSample)) {
    PyErr_SetString(PyExc_ValueError, "samples_u8 second dim too small for MslSample");
    return NULL;
  }
  const int capacity = msl_batch_batch_size(h->batch);
  if (capacity <= 0) {
    PyErr_SetString(PyExc_ValueError, "batch capacity must be positive");
    return NULL;
  }

  PyObject* total_records_obj = PyLong_FromLong(total_records);
  PyObject* num_players_obj = PyLong_FromLong(num_players);
  PyObject* profile_rl1_obj = PyLong_FromLong(profile_rl1);
  PyObject* create_args =
      (total_records_obj != NULL && num_players_obj != NULL && profile_rl1_obj != NULL)
          ? PyTuple_Pack(3, total_records_obj, num_players_obj, profile_rl1_obj)
          : NULL;
  Py_XDECREF(total_records_obj);
  Py_XDECREF(num_players_obj);
  Py_XDECREF(profile_rl1_obj);
  if (create_args == NULL) {
    return NULL;
  }
  PyObject* capsule = msl_one_step_summary_create(NULL, create_args);
  Py_DECREF(create_args);
  if (capsule == NULL) {
    return NULL;
  }
  MslOneStepSummary* summary = msl_one_step_summary_unpack(capsule);
  if (summary == NULL) {
    Py_DECREF(capsule);
    return NULL;
  }

  uint8_t* compare_bytes = (uint8_t*)PyMem_Malloc((size_t)capacity * sizeof(MslCompare));
  uint8_t* seed_tail = (uint8_t*)PyMem_Malloc((size_t)capacity * sizeof(MslSeed));
  if (compare_bytes == NULL || seed_tail == NULL) {
    PyMem_Free(compare_bytes);
    PyMem_Free(seed_tail);
    Py_DECREF(capsule);
    PyErr_NoMemory();
    return NULL;
  }

  const uint8_t* samples_u8 = (const uint8_t*)PyArray_DATA(samples_arr);
  const size_t sample_stride = (size_t)PyArray_STRIDE(samples_arr, 0);
  const size_t seed_off = offsetof(MslSample, seed_t);
  const size_t prev_input_off = offsetof(MslSample, prev_input_t);
  const size_t input_off = offsetof(MslSample, input_t);
  int offset = 0;
  while (offset < total_records) {
    const int chunk_n = (total_records - offset) < capacity ? (total_records - offset) : capacity;
    const uint8_t* chunk_base = samples_u8 + (size_t)offset * sample_stride;
    const uint8_t* seed_bytes = chunk_base + seed_off;
    const uint8_t* prev_bytes = chunk_base + prev_input_off;
    const uint8_t* input_bytes = chunk_base + input_off;
    size_t seed_stride = sample_stride;
    size_t prev_stride = sample_stride;
    size_t input_stride = sample_stride;

    if (chunk_n < capacity) {
      for (int r = 0; r < chunk_n; r++) {
        const MslSample* sample =
            (const MslSample*)(const void*)(chunk_base + (size_t)r * sample_stride);
        memcpy(seed_tail + (size_t)r * sizeof(MslSeed), &sample->seed_t, sizeof(MslSeed));
        memcpy(h->prev_input_storage + (size_t)r * h->prev_input_storage_stride,
               &sample->prev_input_t, sizeof(MslInput));
        memcpy(h->input_storage + (size_t)r * h->input_storage_stride, &sample->input_t,
               sizeof(MslInput));
      }
      for (int r = chunk_n; r < capacity; r++) {
        memcpy(seed_tail + (size_t)r * sizeof(MslSeed), seed_tail, sizeof(MslSeed));
        memcpy(h->prev_input_storage + (size_t)r * h->prev_input_storage_stride,
               h->prev_input_storage, sizeof(MslInput));
        memcpy(h->input_storage + (size_t)r * h->input_storage_stride, h->input_storage,
               sizeof(MslInput));
      }
      seed_bytes = seed_tail;
      prev_bytes = h->prev_input_storage;
      input_bytes = h->input_storage;
      seed_stride = sizeof(MslSeed);
      prev_stride = h->prev_input_storage_stride;
      input_stride = h->input_storage_stride;
    }

    int err = 0;
    PyThreadState* py_thread_state = PyEval_SaveThread();
    err = msl_batch_reseed_seed(h->batch, seed_bytes, seed_stride);
    if (err == 0) {
      err = msl_batch_step_input(h->batch, prev_bytes, prev_stride, input_bytes, input_stride);
    }
    if (err == 0) {
      err = msl_batch_write_compare(h->batch, compare_bytes, sizeof(MslCompare));
    }
    PyEval_RestoreThread(py_thread_state);
    if (err != 0) {
      PyMem_Free(compare_bytes);
      PyMem_Free(seed_tail);
      Py_DECREF(capsule);
      PyErr_Format(PyExc_RuntimeError, "one_step_eval_samples failed: %d", err);
      return NULL;
    }

    npy_intp compare_dims[2] = {(npy_intp)chunk_n, (npy_intp)sizeof(MslCompare)};
    npy_intp sample_dims[2] = {(npy_intp)chunk_n, (npy_intp)sample_stride};
    PyObject* compare_arr = PyArray_SimpleNewFromData(2, compare_dims, NPY_UINT8, compare_bytes);
    PyObject* sample_arr = PyArray_SimpleNewFromData(2, sample_dims, NPY_UINT8, (void*)chunk_base);
    PyObject* accum_args = (compare_arr != NULL && sample_arr != NULL)
                               ? PyTuple_Pack(3, capsule, compare_arr, sample_arr)
                               : NULL;
    PyObject* accum = accum_args != NULL ? msl_one_step_summary_accumulate(NULL, accum_args) : NULL;
    Py_XDECREF(accum_args);
    Py_XDECREF(compare_arr);
    Py_XDECREF(sample_arr);
    if (accum == NULL) {
      PyMem_Free(compare_bytes);
      PyMem_Free(seed_tail);
      Py_DECREF(capsule);
      return NULL;
    }
    Py_DECREF(accum);
    offset += chunk_n;
  }

  PyMem_Free(compare_bytes);
  PyMem_Free(seed_tail);
  PyObject* finish_args = PyTuple_Pack(1, capsule);
  Py_DECREF(capsule);
  if (finish_args == NULL) {
    return NULL;
  }
  PyObject* result = msl_one_step_summary_finish(NULL, finish_args);
  Py_DECREF(finish_args);
  return result;
}

PyObject* msl_one_step_eval_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  PyObject* prev_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* ref_obj = NULL;
  int num_players = 0;
  int profile_rl1 = 0;
  if (!PyArg_ParseTuple(args, "OOOOOii", &handle_obj, &seed_obj, &prev_obj, &input_obj, &ref_obj,
                        &num_players, &profile_rl1)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array_readonly(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* prev_arr =
      require_contiguous_array_readonly(prev_obj, NPY_UINT8, 2, "prev_input_u8");
  PyArrayObject* input_arr = require_contiguous_array_readonly(input_obj, NPY_UINT8, 2, "input_u8");
  PyArrayObject* ref_arr = require_contiguous_array_readonly(ref_obj, NPY_UINT8, 2, "ref_u8");
  if (seed_arr == NULL || prev_arr == NULL || input_arr == NULL || ref_arr == NULL) {
    return NULL;
  }
  const int total_records = (int)PyArray_DIM(seed_arr, 0);
  if (PyArray_DIM(prev_arr, 0) != (npy_intp)total_records ||
      PyArray_DIM(input_arr, 0) != (npy_intp)total_records ||
      PyArray_DIM(ref_arr, 0) != (npy_intp)total_records) {
    PyErr_SetString(PyExc_ValueError, "validation buffers must have matching row counts");
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  if (PyArray_DIM(seed_arr, 1) < (npy_intp)sizeof(MslSeed) ||
      PyArray_DIM(prev_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(ref_arr, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "validation buffer row is smaller than native struct");
    return NULL;
  }
  const int capacity = msl_batch_batch_size(h->batch);
  if (capacity <= 0) {
    PyErr_SetString(PyExc_ValueError, "batch capacity must be positive");
    return NULL;
  }

  PyObject* create_args = Py_BuildValue("iii", total_records, num_players, profile_rl1);
  if (create_args == NULL) {
    return NULL;
  }
  PyObject* capsule = msl_one_step_summary_create(NULL, create_args);
  Py_DECREF(create_args);
  if (capsule == NULL) {
    return NULL;
  }
  MslOneStepSummary* summary = msl_one_step_summary_unpack(capsule);
  if (summary == NULL) {
    Py_DECREF(capsule);
    return NULL;
  }

  uint8_t* compare_bytes = (uint8_t*)PyMem_Malloc((size_t)capacity * sizeof(MslCompare));
  uint8_t* seed_tail = (uint8_t*)PyMem_Malloc((size_t)capacity * sizeof(MslSeed));
  if (compare_bytes == NULL || seed_tail == NULL) {
    PyMem_Free(compare_bytes);
    PyMem_Free(seed_tail);
    Py_DECREF(capsule);
    PyErr_NoMemory();
    return NULL;
  }

  const uint8_t* seed_data = (const uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* prev_data = (const uint8_t*)PyArray_DATA(prev_arr);
  const uint8_t* input_data = (const uint8_t*)PyArray_DATA(input_arr);
  const uint8_t* ref_data = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride0 = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t prev_stride0 = (size_t)PyArray_STRIDE(prev_arr, 0);
  const size_t input_stride0 = (size_t)PyArray_STRIDE(input_arr, 0);
  const size_t ref_stride0 = (size_t)PyArray_STRIDE(ref_arr, 0);

  int offset = 0;
  while (offset < total_records) {
    const int chunk_n = (total_records - offset) < capacity ? (total_records - offset) : capacity;
    const uint8_t* seed_bytes = seed_data + (size_t)offset * seed_stride0;
    const uint8_t* prev_bytes = prev_data + (size_t)offset * prev_stride0;
    const uint8_t* input_bytes = input_data + (size_t)offset * input_stride0;
    size_t seed_stride = seed_stride0;
    size_t prev_stride = prev_stride0;
    size_t input_stride = input_stride0;

    if (chunk_n < capacity) {
      for (int r = 0; r < chunk_n; r++) {
        memcpy(seed_tail + (size_t)r * sizeof(MslSeed), seed_bytes + (size_t)r * seed_stride0,
               sizeof(MslSeed));
        memcpy(h->prev_input_storage + (size_t)r * h->prev_input_storage_stride,
               prev_bytes + (size_t)r * prev_stride0, sizeof(MslInput));
        memcpy(h->input_storage + (size_t)r * h->input_storage_stride,
               input_bytes + (size_t)r * input_stride0, sizeof(MslInput));
      }
      for (int r = chunk_n; r < capacity; r++) {
        memcpy(seed_tail + (size_t)r * sizeof(MslSeed), seed_tail, sizeof(MslSeed));
        memcpy(h->prev_input_storage + (size_t)r * h->prev_input_storage_stride,
               h->prev_input_storage, sizeof(MslInput));
        memcpy(h->input_storage + (size_t)r * h->input_storage_stride, h->input_storage,
               sizeof(MslInput));
      }
      seed_bytes = seed_tail;
      prev_bytes = h->prev_input_storage;
      input_bytes = h->input_storage;
      seed_stride = sizeof(MslSeed);
      prev_stride = h->prev_input_storage_stride;
      input_stride = h->input_storage_stride;
    }

    int err = 0;
    PyThreadState* py_thread_state = PyEval_SaveThread();
    err = msl_batch_reseed_seed(h->batch, seed_bytes, seed_stride);
    if (err == 0) {
      err = msl_batch_step_input(h->batch, prev_bytes, prev_stride, input_bytes, input_stride);
    }
    if (err == 0) {
      err = msl_batch_write_compare(h->batch, compare_bytes, sizeof(MslCompare));
    }
    PyEval_RestoreThread(py_thread_state);
    if (err != 0) {
      PyMem_Free(compare_bytes);
      PyMem_Free(seed_tail);
      Py_DECREF(capsule);
      PyErr_Format(PyExc_RuntimeError, "one_step_eval_buffers failed: %d", err);
      return NULL;
    }

    for (int r = 0; r < chunk_n; r++) {
      const MslCompare* out =
          (const MslCompare*)(const void*)(compare_bytes + (size_t)r * sizeof(MslCompare));
      const MslCompare* ref =
          (const MslCompare*)(const void*)(ref_data + ((size_t)offset + (size_t)r) * ref_stride0);
      if (msl_one_step_summary_accumulate_row(summary, out, ref) != 0) {
        PyMem_Free(compare_bytes);
        PyMem_Free(seed_tail);
        Py_DECREF(capsule);
        return NULL;
      }
    }
    offset += chunk_n;
  }

  PyMem_Free(compare_bytes);
  PyMem_Free(seed_tail);
  PyObject* finish_args = PyTuple_Pack(1, capsule);
  Py_DECREF(capsule);
  if (finish_args == NULL) {
    return NULL;
  }
  PyObject* result = msl_one_step_summary_finish(NULL, finish_args);
  Py_DECREF(finish_args);
  return result;
}

static PyObject* msl_rollout_float_rows_to_py(const int* fields, const int* counts,
                                              MslRolloutFloatTopRow* rows, int field_count,
                                              int top) {
  PyObject* out = PyDict_New();
  if (out == NULL) {
    return NULL;
  }
  for (int fi = 0; fi < field_count; fi++) {
    const char* field_name = msl_rollout_float_field_name(fields[fi]);
    if (field_name == NULL) {
      Py_DECREF(out);
      PyErr_SetString(PyExc_RuntimeError, "invalid rollout float field");
      return NULL;
    }
    const int count = counts[fi];
    PyObject* rows_list = PyList_New(count);
    if (rows_list == NULL) {
      Py_DECREF(out);
      return NULL;
    }
    MslRolloutFloatTopRow* field_rows = rows + (size_t)fi * (size_t)top;
    msl_rollout_float_stable_sort_report_order(field_rows, count);
    for (int ri = 0; ri < count; ri++) {
      const MslRolloutFloatTopRow* row = &field_rows[ri];
      PyObject* row_dict = PyDict_New();
      if (row_dict == NULL || msl_put_string(row_dict, "field", field_name) != 0 ||
          msl_put_double(row_dict, "abs_err", row->abs_err) != 0 ||
          msl_put_long(row_dict, "record", row->record) != 0 ||
          msl_put_long(row_dict, "p", row->player) != 0 ||
          msl_put_long(row_dict, "seed_frame", row->seed_frame) != 0 ||
          msl_put_long(row_dict, "ref_frame", row->ref_frame) != 0 ||
          msl_put_double(row_dict, "seed", row->seed) != 0 ||
          msl_put_double(row_dict, "out", row->out) != 0 ||
          msl_put_double(row_dict, "ref", row->ref) != 0 ||
          msl_put_long(row_dict, "seed_action_id", row->seed_action_id) != 0 ||
          msl_put_long(row_dict, "out_action_id", row->out_action_id) != 0 ||
          msl_put_long(row_dict, "ref_action_id", row->ref_action_id) != 0 ||
          msl_put_long(row_dict, "seed_action_frame", row->seed_action_frame) != 0 ||
          msl_put_long(row_dict, "out_action_frame", row->out_action_frame) != 0 ||
          msl_put_long(row_dict, "ref_action_frame", row->ref_action_frame) != 0 ||
          msl_put_string(row_dict, "attempt",
                         row->attempt_seeded_retry ? "seeded_retry" : "free_run") != 0 ||
          msl_put_bool(row_dict, "seeded_retry", row->attempt_seeded_retry) != 0 ||
          msl_put_bool(row_dict, "discrete_state_matches", row->discrete_state_matches) != 0 ||
          msl_put_long(row_dict, "streak_start_record", row->streak_start_record) != 0 ||
          msl_put_long(row_dict, "streak_len", row->streak_len) != 0) {
        Py_XDECREF(row_dict);
        Py_DECREF(rows_list);
        Py_DECREF(out);
        return NULL;
      }
      PyList_SET_ITEM(rows_list, ri, row_dict);
    }
    if (PyDict_SetItemString(out, field_name, rows_list) != 0) {
      Py_DECREF(rows_list);
      Py_DECREF(out);
      return NULL;
    }
    Py_DECREF(rows_list);
  }
  return out;
}

PyObject* msl_standard_rollout_scan(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* samples_obj = NULL;
  PyObject* players_obj = NULL;
  int num_players = 0;
  int max_records = 0;
  int ucf_enabled = -1;
  int ucf_cardinals = -1;
  int profile_rl1 = 0;
  PyObject* float_fields_obj = NULL;
  int float_top = 0;
  double float_threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOiiiii|Oid", &samples_obj, &players_obj, &num_players, &max_records,
                        &ucf_enabled, &ucf_cardinals, &profile_rl1, &float_fields_obj, &float_top,
                        &float_threshold)) {
    return NULL;
  }
  PyArrayObject* samples =
      require_contiguous_array_readonly(samples_obj, NPY_UINT8, 2, "samples_u8");
  PyArrayObject* players_arr =
      require_contiguous_array_readonly(players_obj, NPY_UINT8, 1, "players");
  if (samples == NULL || players_arr == NULL) {
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
  const uint8_t* players = (const uint8_t*)PyArray_DATA(players_arr);
  const npy_intp player_count = PyArray_DIM(players_arr, 0);
  for (npy_intp i = 0; i < player_count; i++) {
    if (players[i] >= (uint8_t)num_players) {
      PyErr_SetString(PyExc_ValueError, "player index out of range");
      return NULL;
    }
  }
  if (float_top < 0) {
    PyErr_SetString(PyExc_ValueError, "float_top must be non-negative");
    return NULL;
  }

  const int total_records = (int)PyArray_DIM(samples, 0);
  int n = total_records;
  if (max_records > 0 && n > max_records) {
    n = max_records;
  }
  int* float_fields = NULL;
  int* float_counts = NULL;
  MslRolloutFloatTopRow* float_rows = NULL;
  int float_field_count = 0;
  if (float_fields_obj != NULL && float_top > 0) {
    PyObject* seq = PySequence_Fast(float_fields_obj, "float_fields must be a sequence");
    if (seq == NULL) {
      return NULL;
    }
    const Py_ssize_t seq_n = PySequence_Fast_GET_SIZE(seq);
    if (seq_n > 0) {
      float_fields = (int*)PyMem_RawMalloc((size_t)seq_n * sizeof(int));
      float_counts = (int*)PyMem_RawCalloc((size_t)seq_n, sizeof(int));
      float_rows = (MslRolloutFloatTopRow*)PyMem_RawCalloc((size_t)seq_n * (size_t)float_top,
                                                           sizeof(MslRolloutFloatTopRow));
      if (float_fields == NULL || float_counts == NULL || float_rows == NULL) {
        Py_DECREF(seq);
        PyMem_RawFree(float_fields);
        PyMem_RawFree(float_counts);
        PyMem_RawFree(float_rows);
        PyErr_NoMemory();
        return NULL;
      }
      for (Py_ssize_t i = 0; i < seq_n; i++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq, i);
        const char* name = PyUnicode_AsUTF8(item);
        if (name == NULL) {
          Py_DECREF(seq);
          PyMem_RawFree(float_fields);
          PyMem_RawFree(float_counts);
          PyMem_RawFree(float_rows);
          return NULL;
        }
        const int field = msl_rollout_float_field_from_name(name);
        if (field < 0) {
          Py_DECREF(seq);
          PyMem_RawFree(float_fields);
          PyMem_RawFree(float_counts);
          PyMem_RawFree(float_rows);
          PyErr_Format(PyExc_ValueError, "unsupported rollout float field: %s", name);
          return NULL;
        }
        float_fields[i] = field;
      }
      float_field_count = (int)seq_n;
    }
    Py_DECREF(seq);
  }
  int* hist = (int*)PyMem_RawCalloc((size_t)n + 1u, sizeof(int));
  if (hist == NULL) {
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    PyErr_NoMemory();
    return NULL;
  }
  MslBatch* batch = msl_batch_create(1, num_players);
  if (batch == NULL) {
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    PyErr_SetString(PyExc_RuntimeError, "msl_batch_create failed");
    return NULL;
  }
  if (ucf_enabled >= 0) {
    (void)msl_batch_set_ucf_enabled(batch, ucf_enabled != 0);
  }
  if (ucf_cardinals >= 0) {
    (void)msl_batch_set_ucf_cardinals_1_0_enabled(batch, ucf_cardinals != 0);
  }

  const uint8_t* samples_u8 = (const uint8_t*)PyArray_DATA(samples);
  const size_t sample_stride = (size_t)PyArray_STRIDE(samples, 0);
  MslCompare out;
  int best_len = 0;
  int best_start = 0;
  int best_end_excl = 0;
  int cur_start = 0;
  int cur_len = 0;
  int needs_seed = 1;
  int mismatch_fields[7] = {0};
  int mismatch_fields_seeded[7] = {0};
  int ignored_first = 0;
  int ignored_first_seeded = 0;
  int err = 0;

  for (int j = 0; j < n;) {
    if (needs_seed != 0) {
      err = msl_standard_rollout_reseed_at(batch, samples_u8, sample_stride, cur_start);
      if (err != 0) {
        break;
      }
      needs_seed = 0;
    }
    const int attempt = msl_standard_rollout_step_compare(batch, samples_u8, sample_stride, j,
                                                          players, player_count, profile_rl1, &out);
    if (attempt < 0) {
      err = attempt;
      break;
    }
    if ((attempt & 0x100) != 0) {
      ignored_first++;
    }
    const int code = attempt & 0xFF;
    const uint8_t* row = samples_u8 + (size_t)j * sample_stride;
    const MslSeed* seed = (const MslSeed*)(const void*)(row + offsetof(MslSample, seed_t));
    const MslCompare* ref = (const MslCompare*)(const void*)(row + offsetof(MslSample, ref_t1));
    msl_rollout_float_collect(float_rows, float_counts, float_fields, float_field_count, float_top,
                              float_threshold, players, player_count, seed, &out, ref, j, 0,
                              code == 0, cur_start, cur_len);
    if (code == 0) {
      cur_len++;
      if (cur_len > best_len) {
        best_len = cur_len;
        best_start = cur_start;
        best_end_excl = cur_start + cur_len;
      }
      j++;
      continue;
    }
    if (cur_len > 0) {
      hist[cur_len]++;
    }
    if (code >= 1 && code <= 6) {
      mismatch_fields[code]++;
    }

    cur_start = j;
    cur_len = 0;
    err = msl_standard_rollout_reseed_at(batch, samples_u8, sample_stride, j);
    if (err != 0) {
      break;
    }
    const int retry = msl_standard_rollout_step_compare(batch, samples_u8, sample_stride, j,
                                                        players, player_count, profile_rl1, &out);
    if (retry < 0) {
      err = retry;
      break;
    }
    if ((retry & 0x100) != 0) {
      ignored_first_seeded++;
    }
    const int retry_code = retry & 0xFF;
    row = samples_u8 + (size_t)j * sample_stride;
    seed = (const MslSeed*)(const void*)(row + offsetof(MslSample, seed_t));
    ref = (const MslCompare*)(const void*)(row + offsetof(MslSample, ref_t1));
    msl_rollout_float_collect(float_rows, float_counts, float_fields, float_field_count, float_top,
                              float_threshold, players, player_count, seed, &out, ref, j, 1,
                              retry_code == 0, cur_start, 0);
    if (retry_code == 0) {
      cur_len = 1;
      if (cur_len > best_len) {
        best_len = cur_len;
        best_start = cur_start;
        best_end_excl = cur_start + cur_len;
      }
      j++;
      continue;
    }
    if (retry_code >= 1 && retry_code <= 6) {
      mismatch_fields_seeded[retry_code]++;
    }
    cur_start = j + 1;
    cur_len = 0;
    j++;
    needs_seed = 1;
  }

  if (err == 0 && cur_len > 0) {
    hist[cur_len]++;
  }
  msl_batch_destroy(batch);
  if (err != 0) {
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    PyErr_Format(PyExc_RuntimeError, "standard_rollout_scan failed: %d", err);
    return NULL;
  }

  PyObject* result = PyDict_New();
  PyObject* hist_dict = PyDict_New();
  PyObject* mismatch_list = PyList_New(6);
  PyObject* mismatch_seeded_list = PyList_New(6);
  if (result == NULL || hist_dict == NULL || mismatch_list == NULL ||
      mismatch_seeded_list == NULL) {
    Py_XDECREF(result);
    Py_XDECREF(hist_dict);
    Py_XDECREF(mismatch_list);
    Py_XDECREF(mismatch_seeded_list);
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  for (int len = 1; len <= n; len++) {
    if (hist[len] == 0) {
      continue;
    }
    PyObject* key = PyLong_FromLong((long)len);
    PyObject* val = PyLong_FromLong((long)hist[len]);
    if (key == NULL || val == NULL || PyDict_SetItem(hist_dict, key, val) != 0) {
      Py_XDECREF(key);
      Py_XDECREF(val);
      Py_DECREF(result);
      Py_DECREF(hist_dict);
      Py_DECREF(mismatch_list);
      Py_DECREF(mismatch_seeded_list);
      PyMem_RawFree(hist);
      PyMem_RawFree(float_fields);
      PyMem_RawFree(float_counts);
      PyMem_RawFree(float_rows);
      return NULL;
    }
    Py_DECREF(key);
    Py_DECREF(val);
  }
  PyMem_RawFree(hist);
  for (int i = 0; i < 6; i++) {
    PyObject* a = PyLong_FromLong((long)mismatch_fields[i + 1]);
    PyObject* b = PyLong_FromLong((long)mismatch_fields_seeded[i + 1]);
    if (a == NULL || b == NULL) {
      Py_XDECREF(a);
      Py_XDECREF(b);
      Py_DECREF(result);
      Py_DECREF(hist_dict);
      Py_DECREF(mismatch_list);
      Py_DECREF(mismatch_seeded_list);
      PyMem_RawFree(float_fields);
      PyMem_RawFree(float_counts);
      PyMem_RawFree(float_rows);
      return NULL;
    }
    PyList_SET_ITEM(mismatch_list, i, a);
    PyList_SET_ITEM(mismatch_seeded_list, i, b);
  }
  if (msl_put_long(result, "best_len", best_len) != 0 ||
      msl_put_long(result, "best_start_record", best_start) != 0 ||
      msl_put_long(result, "best_end_record_excl", best_end_excl) != 0 ||
      msl_put_long(result, "ignored_first", ignored_first) != 0 ||
      msl_put_long(result, "ignored_first_seeded", ignored_first_seeded) != 0 ||
      PyDict_SetItemString(result, "streak_histogram", hist_dict) != 0 ||
      PyDict_SetItemString(result, "first_mismatch_counts", mismatch_list) != 0 ||
      PyDict_SetItemString(result, "first_mismatch_counts_seeded", mismatch_seeded_list) != 0) {
    Py_DECREF(result);
    Py_DECREF(hist_dict);
    Py_DECREF(mismatch_list);
    Py_DECREF(mismatch_seeded_list);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  PyObject* float_rows_dict = msl_rollout_float_rows_to_py(float_fields, float_counts, float_rows,
                                                           float_field_count, float_top);
  if (float_rows_dict == NULL || PyDict_SetItemString(result, "float_rows", float_rows_dict) != 0) {
    Py_XDECREF(float_rows_dict);
    Py_DECREF(result);
    Py_DECREF(hist_dict);
    Py_DECREF(mismatch_list);
    Py_DECREF(mismatch_seeded_list);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  Py_DECREF(float_rows_dict);
  Py_DECREF(hist_dict);
  Py_DECREF(mismatch_list);
  Py_DECREF(mismatch_seeded_list);
  PyMem_RawFree(float_fields);
  PyMem_RawFree(float_counts);
  PyMem_RawFree(float_rows);
  return result;
}
PyObject* msl_standard_rollout_scan_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* prev_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* players_obj = NULL;
  int num_players = 0;
  int max_records = 0;
  int ucf_enabled = -1;
  int ucf_cardinals = -1;
  int profile_rl1 = 0;
  PyObject* float_fields_obj = NULL;
  int float_top = 0;
  double float_threshold = 0.0;
  int first_mismatch_probe_limit = 0;
  if (!PyArg_ParseTuple(args, "OOOOOiiiii|Oidi", &seed_obj, &prev_obj, &input_obj, &ref_obj,
                        &players_obj, &num_players, &max_records, &ucf_enabled, &ucf_cardinals,
                        &profile_rl1, &float_fields_obj, &float_top, &float_threshold,
                        &first_mismatch_probe_limit)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array_readonly(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* prev_arr =
      require_contiguous_array_readonly(prev_obj, NPY_UINT8, 2, "prev_input_u8");
  PyArrayObject* input_arr = require_contiguous_array_readonly(input_obj, NPY_UINT8, 2, "input_u8");
  PyArrayObject* ref_arr = require_contiguous_array_readonly(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* players_arr =
      require_contiguous_array_readonly(players_obj, NPY_UINT8, 1, "players");
  if (seed_arr == NULL || prev_arr == NULL || input_arr == NULL || ref_arr == NULL ||
      players_arr == NULL) {
    return NULL;
  }
  if (PyArray_DIM(prev_arr, 0) != PyArray_DIM(seed_arr, 0) ||
      PyArray_DIM(input_arr, 0) != PyArray_DIM(seed_arr, 0) ||
      PyArray_DIM(ref_arr, 0) != PyArray_DIM(seed_arr, 0)) {
    PyErr_SetString(PyExc_ValueError, "validation buffers must have matching row counts");
    return NULL;
  }
  if (PyArray_DIM(seed_arr, 1) < (npy_intp)sizeof(MslSeed) ||
      PyArray_DIM(prev_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(ref_arr, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "validation buffer row is smaller than native struct");
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  const uint8_t* players = (const uint8_t*)PyArray_DATA(players_arr);
  const npy_intp player_count = PyArray_DIM(players_arr, 0);
  for (npy_intp i = 0; i < player_count; i++) {
    if (players[i] >= (uint8_t)num_players) {
      PyErr_SetString(PyExc_ValueError, "player index out of range");
      return NULL;
    }
  }
  if (float_top < 0) {
    PyErr_SetString(PyExc_ValueError, "float_top must be non-negative");
    return NULL;
  }

  const int total_records = (int)PyArray_DIM(seed_arr, 0);
  int n = total_records;
  if (max_records > 0 && n > max_records) {
    n = max_records;
  }
  int* float_fields = NULL;
  int* float_counts = NULL;
  MslRolloutFloatTopRow* float_rows = NULL;
  int float_field_count = 0;
  if (float_fields_obj != NULL && float_top > 0) {
    PyObject* seq = PySequence_Fast(float_fields_obj, "float_fields must be a sequence");
    if (seq == NULL) {
      return NULL;
    }
    const Py_ssize_t seq_n = PySequence_Fast_GET_SIZE(seq);
    if (seq_n > 0) {
      float_fields = (int*)PyMem_RawMalloc((size_t)seq_n * sizeof(int));
      float_counts = (int*)PyMem_RawCalloc((size_t)seq_n, sizeof(int));
      float_rows = (MslRolloutFloatTopRow*)PyMem_RawCalloc((size_t)seq_n * (size_t)float_top,
                                                           sizeof(MslRolloutFloatTopRow));
      if (float_fields == NULL || float_counts == NULL || float_rows == NULL) {
        Py_DECREF(seq);
        PyMem_RawFree(float_fields);
        PyMem_RawFree(float_counts);
        PyMem_RawFree(float_rows);
        PyErr_NoMemory();
        return NULL;
      }
      for (Py_ssize_t i = 0; i < seq_n; i++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq, i);
        const char* name = PyUnicode_AsUTF8(item);
        if (name == NULL) {
          Py_DECREF(seq);
          PyMem_RawFree(float_fields);
          PyMem_RawFree(float_counts);
          PyMem_RawFree(float_rows);
          return NULL;
        }
        const int field = msl_rollout_float_field_from_name(name);
        if (field < 0) {
          Py_DECREF(seq);
          PyMem_RawFree(float_fields);
          PyMem_RawFree(float_counts);
          PyMem_RawFree(float_rows);
          PyErr_Format(PyExc_ValueError, "unsupported rollout float field: %s", name);
          return NULL;
        }
        float_fields[i] = field;
      }
      float_field_count = (int)seq_n;
    }
    Py_DECREF(seq);
  }
  int* hist = (int*)PyMem_RawCalloc((size_t)n + 1u, sizeof(int));
  if (hist == NULL) {
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    PyErr_NoMemory();
    return NULL;
  }
  PyObject* first_rows = PyList_New(0);
  if (first_rows == NULL) {
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  MslBatch* batch = msl_batch_create(1, num_players);
  if (batch == NULL) {
    Py_DECREF(first_rows);
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    PyErr_SetString(PyExc_RuntimeError, "msl_batch_create failed");
    return NULL;
  }
  if (ucf_enabled >= 0) {
    (void)msl_batch_set_ucf_enabled(batch, ucf_enabled != 0);
  }
  if (ucf_cardinals >= 0) {
    (void)msl_batch_set_ucf_cardinals_1_0_enabled(batch, ucf_cardinals != 0);
  }

  const uint8_t* seed_u8 = (const uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* prev_u8 = (const uint8_t*)PyArray_DATA(prev_arr);
  const uint8_t* input_u8 = (const uint8_t*)PyArray_DATA(input_arr);
  const uint8_t* ref_u8 = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_arr, 0);
  const size_t input_stride = (size_t)PyArray_STRIDE(input_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  MslCompare out;
  int best_len = 0;
  int best_start = 0;
  int best_end_excl = 0;
  int cur_start = 0;
  int cur_len = 0;
  int needs_seed = 1;
  int mismatch_fields[7] = {0};
  int mismatch_fields_seeded[7] = {0};
  int ignored_first = 0;
  int ignored_first_seeded = 0;
  int err = 0;

  for (int j = 0; j < n;) {
    if (needs_seed != 0) {
      err = msl_standard_rollout_reseed_at_buffers(batch, seed_u8, seed_stride, cur_start);
      if (err != 0) {
        break;
      }
      needs_seed = 0;
    }
    const int attempt = msl_standard_rollout_step_compare_buffers(
        batch, seed_u8, seed_stride, prev_u8, prev_stride, input_u8, input_stride, ref_u8,
        ref_stride, j, players, player_count, profile_rl1, &out);
    if (attempt < 0) {
      err = attempt;
      break;
    }
    if ((attempt & 0x100) != 0) {
      ignored_first++;
    }
    const int code = attempt & 0xFF;
    const MslSeed* seed = (const MslSeed*)(const void*)(seed_u8 + (size_t)j * seed_stride);
    const MslCompare* ref = (const MslCompare*)(const void*)(ref_u8 + (size_t)j * ref_stride);
    msl_rollout_float_collect(float_rows, float_counts, float_fields, float_field_count, float_top,
                              float_threshold, players, player_count, seed, &out, ref, j, 0,
                              code == 0, cur_start, cur_len);
    if (code == 0) {
      cur_len++;
      if (cur_len > best_len) {
        best_len = cur_len;
        best_start = cur_start;
        best_end_excl = cur_start + cur_len;
      }
      j++;
      continue;
    }
    if (cur_len > 0) {
      hist[cur_len]++;
    }
    if (code >= 1 && code <= 6) {
      mismatch_fields[code]++;
    }
    if (first_mismatch_probe_limit > 0 && j < first_mismatch_probe_limit &&
        msl_standard_rollout_append_first_row(first_rows, j, seed, &out, ref, players, player_count,
                                              profile_rl1, 0, cur_start, cur_len) != 0) {
      err = -3;
      break;
    }

    cur_start = j;
    cur_len = 0;
    err = msl_standard_rollout_reseed_at_buffers(batch, seed_u8, seed_stride, j);
    if (err != 0) {
      break;
    }
    const int retry = msl_standard_rollout_step_compare_buffers(
        batch, seed_u8, seed_stride, prev_u8, prev_stride, input_u8, input_stride, ref_u8,
        ref_stride, j, players, player_count, profile_rl1, &out);
    if (retry < 0) {
      err = retry;
      break;
    }
    if ((retry & 0x100) != 0) {
      ignored_first_seeded++;
    }
    const int retry_code = retry & 0xFF;
    seed = (const MslSeed*)(const void*)(seed_u8 + (size_t)j * seed_stride);
    ref = (const MslCompare*)(const void*)(ref_u8 + (size_t)j * ref_stride);
    msl_rollout_float_collect(float_rows, float_counts, float_fields, float_field_count, float_top,
                              float_threshold, players, player_count, seed, &out, ref, j, 1,
                              retry_code == 0, cur_start, 0);
    if (retry_code == 0) {
      cur_len = 1;
      if (cur_len > best_len) {
        best_len = cur_len;
        best_start = cur_start;
        best_end_excl = cur_start + cur_len;
      }
      j++;
      continue;
    }
    if (retry_code >= 1 && retry_code <= 6) {
      mismatch_fields_seeded[retry_code]++;
    }
    if (first_mismatch_probe_limit > 0 && j < first_mismatch_probe_limit &&
        msl_standard_rollout_append_first_row(first_rows, j, seed, &out, ref, players, player_count,
                                              profile_rl1, 1, cur_start, 0) != 0) {
      err = -3;
      break;
    }
    cur_start = j + 1;
    cur_len = 0;
    j++;
    needs_seed = 1;
  }

  if (err == 0 && cur_len > 0) {
    hist[cur_len]++;
  }
  msl_batch_destroy(batch);
  if (err != 0) {
    Py_DECREF(first_rows);
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    if (!PyErr_Occurred()) {
      PyErr_Format(PyExc_RuntimeError, "standard_rollout_scan failed: %d", err);
    }
    return NULL;
  }

  PyObject* result = PyDict_New();
  PyObject* hist_dict = PyDict_New();
  PyObject* mismatch_list = PyList_New(6);
  PyObject* mismatch_seeded_list = PyList_New(6);
  if (result == NULL || hist_dict == NULL || mismatch_list == NULL ||
      mismatch_seeded_list == NULL) {
    Py_XDECREF(result);
    Py_XDECREF(hist_dict);
    Py_XDECREF(mismatch_list);
    Py_XDECREF(mismatch_seeded_list);
    Py_DECREF(first_rows);
    PyMem_RawFree(hist);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  for (int len = 1; len <= n; len++) {
    if (hist[len] == 0) {
      continue;
    }
    PyObject* key = PyLong_FromLong((long)len);
    PyObject* val = PyLong_FromLong((long)hist[len]);
    if (key == NULL || val == NULL || PyDict_SetItem(hist_dict, key, val) != 0) {
      Py_XDECREF(key);
      Py_XDECREF(val);
      Py_DECREF(result);
      Py_DECREF(hist_dict);
      Py_DECREF(mismatch_list);
      Py_DECREF(mismatch_seeded_list);
      Py_DECREF(first_rows);
      PyMem_RawFree(hist);
      PyMem_RawFree(float_fields);
      PyMem_RawFree(float_counts);
      PyMem_RawFree(float_rows);
      return NULL;
    }
    Py_DECREF(key);
    Py_DECREF(val);
  }
  PyMem_RawFree(hist);
  for (int i = 0; i < 6; i++) {
    PyObject* a = PyLong_FromLong((long)mismatch_fields[i + 1]);
    PyObject* b = PyLong_FromLong((long)mismatch_fields_seeded[i + 1]);
    if (a == NULL || b == NULL) {
      Py_XDECREF(a);
      Py_XDECREF(b);
      Py_DECREF(result);
      Py_DECREF(hist_dict);
      Py_DECREF(mismatch_list);
      Py_DECREF(mismatch_seeded_list);
      Py_DECREF(first_rows);
      PyMem_RawFree(float_fields);
      PyMem_RawFree(float_counts);
      PyMem_RawFree(float_rows);
      return NULL;
    }
    PyList_SET_ITEM(mismatch_list, i, a);
    PyList_SET_ITEM(mismatch_seeded_list, i, b);
  }
  if (msl_put_long(result, "best_len", best_len) != 0 ||
      msl_put_long(result, "best_start_record", best_start) != 0 ||
      msl_put_long(result, "best_end_record_excl", best_end_excl) != 0 ||
      msl_put_long(result, "ignored_first", ignored_first) != 0 ||
      msl_put_long(result, "ignored_first_seeded", ignored_first_seeded) != 0 ||
      PyDict_SetItemString(result, "streak_histogram", hist_dict) != 0 ||
      PyDict_SetItemString(result, "first_mismatch_counts", mismatch_list) != 0 ||
      PyDict_SetItemString(result, "first_mismatch_counts_seeded", mismatch_seeded_list) != 0 ||
      PyDict_SetItemString(result, "first_mismatch_rows", first_rows) != 0) {
    Py_DECREF(result);
    Py_DECREF(hist_dict);
    Py_DECREF(mismatch_list);
    Py_DECREF(mismatch_seeded_list);
    Py_DECREF(first_rows);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  PyObject* float_rows_dict = msl_rollout_float_rows_to_py(float_fields, float_counts, float_rows,
                                                           float_field_count, float_top);
  if (float_rows_dict == NULL || PyDict_SetItemString(result, "float_rows", float_rows_dict) != 0) {
    Py_XDECREF(float_rows_dict);
    Py_DECREF(result);
    Py_DECREF(hist_dict);
    Py_DECREF(mismatch_list);
    Py_DECREF(mismatch_seeded_list);
    Py_DECREF(first_rows);
    PyMem_RawFree(float_fields);
    PyMem_RawFree(float_counts);
    PyMem_RawFree(float_rows);
    return NULL;
  }
  Py_DECREF(float_rows_dict);
  Py_DECREF(hist_dict);
  Py_DECREF(mismatch_list);
  Py_DECREF(mismatch_seeded_list);
  Py_DECREF(first_rows);
  PyMem_RawFree(float_fields);
  PyMem_RawFree(float_counts);
  PyMem_RawFree(float_rows);
  return result;
}
