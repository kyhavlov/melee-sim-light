/* Native validation buffer materialization for fighter input/history seed lanes. */

#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY

#include "msl_validation_fighter_history.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/api.h"
#include "../src/action_ids.h"
#include "../src/ids.h"
#include "../src/input_axis.h"
#include "../src/ucf.h"

static PyArrayObject* vh_require(PyObject* obj, int typenum, int ndim, const char* name) {
  return require_contiguous_array(obj, typenum, ndim, name);
}

static int vh_check_len(PyArrayObject* arr, npy_intp n, const char* name) {
  if (PyArray_SIZE(arr) != n) {
    PyErr_Format(PyExc_ValueError, "%s length mismatch", name);
    return -1;
  }
  return 0;
}

static int vh_seed_rows(PyArrayObject* seed, npy_intp n_samples) {
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

static float vh_randf_after_pre_gate(uint32_t seed_in, int stream_offset_steps, int consume_count) {
  uint32_t seed = seed_in;
  const int steps = stream_offset_steps + consume_count + 1;
  for (int i = 0; i < steps; i++) {
    seed = seed * 214013u + 2531011u;
  }
  return (float)((seed >> 16) & 0xFFFFu) * (1.0f / 65536.0f);
}

static int vh_local_slot_from_source_port(const MslSeed* row, int players, int source_port0_raw) {
  for (int p = 0; p < players; p++) {
    if ((int)row->source_port0[p] == source_port0_raw) return p;
  }
  return -1;
}

static int vh_f26_source_port_for_player(const MslSeed* seed, const MslCompare* ref, int players,
                                         int p) {
  const int source = (int)seed->last_hit_by[p];
  if (vh_local_slot_from_source_port(seed, players, source) >= 0) return source;
  return (int)ref->last_hit_by[p];
}

static inline bool vh_action_is_catch_family(uint16_t action_id) {
  return action_id >= (uint16_t)MSL_ACT_CATCH && action_id <= (uint16_t)MSL_ACT_CATCH_CUT;
}

static inline bool vh_action_is_basic_grounded_attack(uint16_t action_id) {
  return action_id >= (uint16_t)MSL_ACT_ATTACK_11 && action_id <= (uint16_t)MSL_ACT_ATTACK_LW4;
}

static inline bool vh_grounded_f26_family(uint16_t action_id, int allow_kneebend) {
  return vh_action_is_catch_family(action_id) || vh_action_is_basic_grounded_attack(action_id) ||
         action_id == (uint16_t)MSL_ACT_DASH ||
         (allow_kneebend != 0 && action_id == (uint16_t)MSL_ACT_KNEE_BEND);
}

static inline bool vh_f26_current_allows_marker(uint16_t action_id, uint8_t on_ground,
                                                int allow_grounded_kneebend) {
  if (on_ground == 0u) {
    return action_id == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_B;
  }
  return vh_grounded_f26_family(action_id, allow_grounded_kneebend);
}

static int vh_f26_current_pre_action_marker(const MslSeed* seed, const MslCompare* ref, int p,
                                            int stream_offset_steps, float roll_prob,
                                            int allow_grounded_kneebend) {
  const uint16_t cur = seed->action_id[p];
  if (seed->hitlag[p] != 0u || seed->hitstun[p] != 0u) return 0;
  if (!vh_f26_current_allows_marker(cur, seed->on_ground[p], allow_grounded_kneebend)) return 0;
  float rolls[4];
  for (int consume = 0; consume < 4; consume++) {
    rolls[consume] =
        vh_randf_after_pre_gate(seed->frame_pre_random_seed, stream_offset_steps, consume);
  }
  if (ref->action_id[p] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
    for (int consume = 0; consume < 4; consume++) {
      if (rolls[consume] < roll_prob) return consume > 0 ? consume : 4;
    }
    return 0;
  }
  if (rolls[0] < roll_prob) {
    for (int consume = 1; consume < 4; consume++) {
      if (rolls[consume] >= roll_prob) return consume;
    }
  }
  return 0;
}

static int vh_f26_marker_stream_steps(int marker) {
  if (marker <= 0) return 0;
  return (marker == 4 ? 0 : marker) + 1;
}

static int vh_f26_prior_same_frame_stream_steps(const MslSeed* seed, const MslCompare* ref,
                                                int players, int p, float roll_prob,
                                                int allow_grounded_kneebend) {
  const int cur_source = vh_f26_source_port_for_player(seed, ref, players, p);
  const int cur_attacker = vh_local_slot_from_source_port(seed, players, cur_source);
  if (cur_attacker < 0) return 0;
  int stream_steps = 0;
  for (int q = 0; q < players; q++) {
    if (q == p) continue;
    const int other_source = vh_f26_source_port_for_player(seed, ref, players, q);
    const int other_attacker = vh_local_slot_from_source_port(seed, players, other_source);
    if (other_attacker < 0 || other_attacker >= cur_attacker) continue;
    const int marker = vh_f26_current_pre_action_marker(seed, ref, q, stream_steps, roll_prob,
                                                        allow_grounded_kneebend);
    stream_steps += vh_f26_marker_stream_steps(marker);
  }
  return stream_steps;
}

static uint8_t vh_fighter_8006cda4_marker(const MslSeed* seed, const MslCompare* ref, int players,
                                          int victim_port, float roll_prob,
                                          int allow_grounded_kneebend) {
  const uint16_t cur = seed->action_id[victim_port];
  const int16_t af = seed->action_frame[victim_port];
  if (cur == 74u) return (uint8_t)((af == 0 || af >= 16) ? 2 : 1);
  if (cur == 363u) return (uint8_t)(af > 3 ? 1u : 0u);
  if (cur == 57u) return 1u;
  if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_N && seed->on_ground[victim_port] == 0u &&
      seed->hitlag[victim_port] == 0u && seed->hitstun[victim_port] == 0u) {
    if (ref->action_id[victim_port] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
      const int steps = vh_f26_prior_same_frame_stream_steps(seed, ref, players, victim_port,
                                                             roll_prob, allow_grounded_kneebend);
      return (uint8_t)vh_f26_current_pre_action_marker(seed, ref, victim_port, steps, roll_prob,
                                                       allow_grounded_kneebend);
    }
    return 0u;
  }
  if (seed->on_ground[victim_port] != 0u && seed->hitlag[victim_port] == 0u &&
      seed->hitstun[victim_port] == 0u &&
      (vh_action_is_catch_family(cur) ||
       ((vh_action_is_basic_grounded_attack(cur) || cur == (uint16_t)MSL_ACT_DASH ||
         (allow_grounded_kneebend != 0 && cur == (uint16_t)MSL_ACT_KNEE_BEND)) &&
        ref->action_id[victim_port] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL))) {
    const int steps = vh_f26_prior_same_frame_stream_steps(seed, ref, players, victim_port,
                                                           roll_prob, allow_grounded_kneebend);
    const int marker = vh_f26_current_pre_action_marker(seed, ref, victim_port, steps, roll_prob,
                                                        allow_grounded_kneebend);
    return (uint8_t)(marker != 0 ? marker : 0);
  }
  if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_B && seed->on_ground[victim_port] == 0u &&
      seed->hitlag[victim_port] == 0u && seed->hitstun[victim_port] == 0u) {
    const int steps = vh_f26_prior_same_frame_stream_steps(seed, ref, players, victim_port,
                                                           roll_prob, allow_grounded_kneebend);
    const int marker = vh_f26_current_pre_action_marker(seed, ref, victim_port, steps, roll_prob,
                                                        allow_grounded_kneebend);
    return (uint8_t)(marker != 0 ? marker : 0);
  }
  if (cur == 239u && seed->on_ground[victim_port] != 0u && seed->hitlag[victim_port] > 0u &&
      seed->hitstun[victim_port] == 0u && (seed->state_flags[victim_port][1] & 0x10u) != 0u) {
    return 2u;
  }
  if (cur == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && seed->on_ground[victim_port] == 0u &&
      seed->hitlag[victim_port] == 0u && seed->hitstun[victim_port] > 0u) {
    const int attacker =
        vh_local_slot_from_source_port(seed, players, (int)seed->last_hit_by[victim_port]);
    if (attacker < 0 || attacker == victim_port) return 0u;
    const uint16_t attacker_action = seed->action_id[attacker];
    const int attacker_frame = (int)seed->action_frame[attacker];
    if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
        (attacker_frame == 3 || attacker_frame == 4)) {
      float rolls[4];
      for (int consume = 0; consume < 4; consume++) {
        rolls[consume] = vh_randf_after_pre_gate(seed->frame_pre_random_seed, 0, consume);
      }
      if (ref->action_id[victim_port] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
        for (int consume = 0; consume < 4; consume++) {
          if (rolls[consume] < roll_prob) return (uint8_t)(consume > 0 ? consume : 4);
        }
        return 0u;
      }
      if (rolls[0] < roll_prob) {
        for (int consume = 1; consume < 4; consume++) {
          if (rolls[consume] >= roll_prob) return (uint8_t)consume;
        }
        return 0u;
      }
    }
    if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_B && attacker_frame >= 6) return 2u;
  }
  return 0u;
}

PyObject* msl_validation_derive_fighter_8006cda4_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  double roll_prob_d = 0.0;
  int num_players = 0;
  int allow_grounded_kneebend = 0;
  if (!PyArg_ParseTuple(args, "OOdii", &seed_obj, &ref_obj, &roll_prob_d, &num_players,
                        &allow_grounded_kneebend)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  if (seed_arr == NULL || ref_arr == NULL) return NULL;
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (PyArray_DIM(ref_arr, 0) != n || vh_seed_rows(seed_arr, n) != 0 ||
      PyArray_NDIM(ref_arr) != 2 || PyArray_DIM(ref_arr, 1) < (npy_intp)sizeof(MslCompare)) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "fighter_8006cda4 validation buffers are invalid");
    }
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* ref_u8 = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  const float roll_prob = (float)roll_prob_d;

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
    const MslCompare* ref = vh_ref_const_at(ref_u8, ref_stride, i);
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      seed->fighter_8006cda4_pre_gate_consume_count[p] = 0u;
    }
    for (int p = 0; p < players; p++) {
      seed->fighter_8006cda4_pre_gate_consume_count[p] =
          vh_fighter_8006cda4_marker(seed, ref, players, p, roll_prob, allow_grounded_kneebend);
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
    const MslCompare* ref = vh_ref_const_at(ref_u8, ref_stride, i);
    for (int p = 0; p < players; p++) {
      const int consume = (int)seed->fighter_8006cda4_pre_gate_consume_count[p];
      if (consume <= 0 || consume > 4) continue;
      if (!(seed->action_id[p] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && seed->on_ground[p] == 0u &&
            seed->hitlag[p] == 0u && seed->hitstun[p] > 0u)) {
        continue;
      }
      const int attacker = vh_local_slot_from_source_port(seed, players, (int)seed->last_hit_by[p]);
      if (attacker < 0 || attacker == p) continue;
      if (seed->action_id[attacker] != (uint16_t)MSL_ACT_ATTACK_AIR_B) continue;
      npy_intp j = i - 1;
      while (j >= 0) {
        MslSeed* prev = vh_seed_at(seed_u8, seed_stride, j);
        if (prev->action_id[p] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) break;
        if (prev->on_ground[p] != 0u || prev->hitstun[p] == 0u) break;
        if (vh_local_slot_from_source_port(prev, players, (int)prev->last_hit_by[p]) != attacker) {
          break;
        }
        if (prev->fighter_8006cda4_pre_gate_consume_count[p] == 0u) {
          prev->fighter_8006cda4_pre_gate_consume_count[p] = (uint8_t)consume;
        }
        j--;
      }
      (void)ref;
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
    const MslCompare* ref = vh_ref_const_at(ref_u8, ref_stride, i);
    for (int p = 0; p < players; p++) {
      const int consume = (int)seed->fighter_8006cda4_pre_gate_consume_count[p];
      if (consume <= 0 || consume > 3) continue;
      if (!(seed->action_id[p] == (uint16_t)MSL_ACT_ATTACK_AIR_N && seed->on_ground[p] == 0u &&
            seed->hitlag[p] == 0u && seed->hitstun[p] == 0u &&
            ref->action_id[p] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL)) {
        continue;
      }
      const int source_port_raw = vh_f26_source_port_for_player(seed, ref, players, p);
      const int attacker = vh_local_slot_from_source_port(seed, players, source_port_raw);
      if (attacker < 0 || attacker == p) continue;
      const uint16_t attacker_action = seed->action_id[attacker];
      npy_intp j = i - 1;
      bool seen_damagefall_handoff = false;
      while (j >= 0) {
        MslSeed* prev = vh_seed_at(seed_u8, seed_stride, j);
        const uint16_t cur = prev->action_id[p];
        if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_N) {
          if (prev->on_ground[p] != 0u || prev->hitlag[p] != 0u || prev->hitstun[p] != 0u) break;
        } else if (cur == (uint16_t)MSL_ACT_DAMAGE_FALL) {
          if (seen_damagefall_handoff) break;
          if (prev->on_ground[p] != 0u || prev->hitlag[p] != 0u || prev->hitstun[p] != 0u) break;
          seen_damagefall_handoff = true;
        } else if (cur == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
          if (!seen_damagefall_handoff) break;
          if (prev->on_ground[p] != 0u || prev->hitlag[p] != 0u || prev->hitstun[p] <= 0u) break;
        } else {
          break;
        }
        if (vh_local_slot_from_source_port(prev, players, source_port_raw) != attacker) break;
        if (cur != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
            prev->action_id[attacker] != attacker_action) {
          break;
        }
        if (prev->fighter_8006cda4_pre_gate_consume_count[p] == 0u) {
          prev->fighter_8006cda4_pre_gate_consume_count[p] = (uint8_t)consume;
        }
        j--;
      }
    }
  }

  Py_RETURN_NONE;
}

static inline uint8_t vh_sat_inc_fe(uint8_t v) { return v < 0xFEu ? (uint8_t)(v + 1u) : 0xFEu; }

static inline uint8_t vh_sat_inc_ff(uint8_t v) { return v < 0xFFu ? (uint8_t)(v + 1u) : 0xFFu; }

static inline float vh_apply_deadzone(float v, float dz) {
  if (v > -dz && v < dz) return 0.0f;
  return v;
}

static inline bool vh_lb_8000d148(float prev_x, float prev_y, float cur_x, float cur_y,
                                  float center_x, float center_y, float threshold) {
  const float diff_01_y = prev_y - cur_y;
  const float diff_01_x = cur_x - prev_x;
  const float dist_squared_01 = diff_01_x * diff_01_x + diff_01_y * diff_01_y;
  if (dist_squared_01 < 0.00001f) return false;
  const float dist_01 = sqrtf(dist_squared_01);
  float var_f0 =
      ((prev_x * cur_y) - (prev_y * cur_x)) + ((diff_01_x * center_x) + (diff_01_y * center_y));
  if (var_f0 < 0.0f) var_f0 = -var_f0;
  if ((var_f0 / dist_01) > threshold) return false;
  const float diff_02_x = prev_x - center_x;
  const float diff_02_y = prev_y - center_y;
  const float diff_12_x = cur_x - center_x;
  const float diff_12_y = cur_y - center_y;
  const float threshold_squared = threshold * threshold;
  const float dist_squared_02 = diff_02_x * diff_02_x + diff_02_y * diff_02_y;
  const float dist_squared_12 = diff_12_x * diff_12_x + diff_12_y * diff_12_y;
  if (dist_squared_02 < threshold_squared) {
    if (dist_squared_12 > threshold_squared) return true;
    if (dist_squared_12 < threshold_squared) return false;
    return true;
  }
  if (dist_squared_02 > threshold_squared) {
    if (dist_squared_12 > threshold_squared) {
      if (((prev_x > center_x) && (cur_x < center_x)) ||
          ((prev_x < center_x) && (cur_x > center_x)) ||
          ((prev_y > center_y) && (cur_y < center_y)) ||
          ((prev_y < center_y) && (cur_y > center_y))) {
        return true;
      }
      return false;
    }
    if (dist_squared_12 < threshold_squared) return true;
    return true;
  }
  return true;
}

static inline float vh_ucf_popo_to_nana(float x) {
  if (x >= 0.0f) {
    return (float)((int8_t)(x * 127.0f)) / 127.0f;
  }
  return (float)((int8_t)(x * 128.0f)) / 128.0f;
}

static int vh_make_1d(npy_intp n, int typenum, PyArrayObject** out) {
  npy_intp dims[1] = {n};
  *out = (PyArrayObject*)PyArray_EMPTY(1, dims, typenum, 0);
  return *out == NULL ? -1 : 0;
}

PyObject* msl_validation_derive_guard_input_prefix_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject *seed_obj = NULL, *buttons_obj = NULL, *main_x_obj = NULL, *main_y_obj = NULL;
  PyObject *c_x_obj = NULL, *c_y_obj = NULL, *l_obj = NULL, *r_obj = NULL;
  PyObject *char_obj = NULL, *action_obj = NULL, *frame_obj = NULL, *facing_obj = NULL;
  PyObject *shield_obj = NULL, *hitlag_obj = NULL, *flags_obj = NULL;
  PyObject *neutral_obj = NULL, *frame_max_obj = NULL;
  int slot = 0;
  int ucf_enabled = 0;
  int cardinals = 0;
  double deadzone_x = 0.0, deadzone_y = 0.0, guard_lerp = 0.0;
  int button_mask_lr = 0, button_mask_z = 0;
  double trigger_deadzone = 0.0;
  int guard_x10_init = 0;
  int act_guard_on = 0, act_guard = 0, act_guard_off = 0, act_guard_reflect = 0;
  int act_guard_set_off = 0;
  int guard_special_enable_frames = 0;
  double hitlag_dmg_mul = 0.0, hitlag_base = 0.0;
  int powershield_reflect_frames = 0, powershield_reflect_total_frames = 0;
  if (!PyArg_ParseTuple(args, "OiOOOOOOOOOOOOOOOOiidddiidiiiiiiiddii", &seed_obj, &slot,
                        &buttons_obj, &main_x_obj, &main_y_obj, &c_x_obj, &c_y_obj, &l_obj, &r_obj,
                        &char_obj, &action_obj, &frame_obj, &facing_obj, &shield_obj, &hitlag_obj,
                        &flags_obj, &neutral_obj, &frame_max_obj, &ucf_enabled, &cardinals,
                        &deadzone_x, &deadzone_y, &guard_lerp, &button_mask_lr, &button_mask_z,
                        &trigger_deadzone, &guard_x10_init, &act_guard_on, &act_guard,
                        &act_guard_off, &act_guard_reflect, &act_guard_set_off,
                        &guard_special_enable_frames, &hitlag_dmg_mul, &hitlag_base,
                        &powershield_reflect_frames, &powershield_reflect_total_frames)) {
    return NULL;
  }

  PyArrayObject* seed_arr = vh_require(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* buttons_arr = vh_require(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* main_x_arr = vh_require(main_x_obj, NPY_INT8, 1, "main_x");
  PyArrayObject* main_y_arr = vh_require(main_y_obj, NPY_INT8, 1, "main_y");
  PyArrayObject* c_x_arr = vh_require(c_x_obj, NPY_INT8, 1, "c_x");
  PyArrayObject* c_y_arr = vh_require(c_y_obj, NPY_INT8, 1, "c_y");
  PyArrayObject* l_arr = vh_require(l_obj, NPY_UINT8, 1, "l");
  PyArrayObject* r_arr = vh_require(r_obj, NPY_UINT8, 1, "r");
  PyArrayObject* char_arr = vh_require(char_obj, NPY_UINT8, 1, "char_id");
  PyArrayObject* action_arr = vh_require(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame_arr = vh_require(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* facing_arr = vh_require(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* shield_arr = vh_require(shield_obj, NPY_FLOAT32, 1, "shield_hp");
  PyArrayObject* hitlag_arr = vh_require(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* flags_arr = vh_require(flags_obj, NPY_UINT8, 2, "state_flags");
  PyArrayObject* neutral_arr = vh_require(neutral_obj, NPY_UINT16, 1, "neutral_frame");
  PyArrayObject* frame_max_arr = vh_require(frame_max_obj, NPY_UINT16, 1, "frame_max");
  if (seed_arr == NULL || buttons_arr == NULL || main_x_arr == NULL || main_y_arr == NULL ||
      c_x_arr == NULL || c_y_arr == NULL || l_arr == NULL || r_arr == NULL || char_arr == NULL ||
      action_arr == NULL || frame_arr == NULL || facing_arr == NULL || shield_arr == NULL ||
      hitlag_arr == NULL || flags_arr == NULL || neutral_arr == NULL || frame_max_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action_arr);
  const npy_intp n_samples = n - 1;
  if (n < 1) {
    PyErr_SetString(PyExc_ValueError, "action_id must contain at least one frame");
    return NULL;
  }
  if (slot < 0 || slot >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "slot out of range");
    return NULL;
  }
  if (vh_seed_rows(seed_arr, n_samples) != 0 || vh_check_len(buttons_arr, n, "buttons") != 0 ||
      vh_check_len(main_x_arr, n, "main_x") != 0 || vh_check_len(main_y_arr, n, "main_y") != 0 ||
      vh_check_len(c_x_arr, n, "c_x") != 0 || vh_check_len(c_y_arr, n, "c_y") != 0 ||
      vh_check_len(l_arr, n, "l") != 0 || vh_check_len(r_arr, n, "r") != 0 ||
      vh_check_len(char_arr, n, "char_id") != 0 ||
      vh_check_len(frame_arr, n, "action_frame") != 0 ||
      vh_check_len(facing_arr, n, "facing") != 0 || vh_check_len(shield_arr, n, "shield_hp") != 0 ||
      vh_check_len(hitlag_arr, n, "hitlag") != 0 ||
      vh_check_len(neutral_arr, n, "neutral_frame") != 0 ||
      vh_check_len(frame_max_arr, n, "frame_max") != 0) {
    return NULL;
  }
  if (PyArray_NDIM(flags_arr) != 2 || PyArray_DIM(flags_arr, 0) != n ||
      PyArray_DIM(flags_arr, 1) < 4) {
    PyErr_SetString(PyExc_ValueError, "state_flags must be uint8[n, >=4]");
    return NULL;
  }
  const float trigger_dz = (float)trigger_deadzone;
  const float trigger_denom = 1.0f - trigger_dz;
  if (!(trigger_denom > 0.0f)) {
    PyErr_SetString(PyExc_ValueError, "invalid trigger_deadzone");
    return NULL;
  }

  PyArrayObject *main_x_proc_arr = NULL, *main_y_proc_arr = NULL, *c_x_proc_arr = NULL;
  PyArrayObject *c_y_proc_arr = NULL, *stick_x_arr = NULL, *stick_y_arr = NULL,
                *cstick_y_arr = NULL;
  PyArrayObject *trigger_arr = NULL, *pressed_arr = NULL, *lr_timer_arr = NULL;
  PyArrayObject *light_arr = NULL, *guard_phase_arr = NULL;
  if (vh_make_1d(n, NPY_INT8, &main_x_proc_arr) != 0 ||
      vh_make_1d(n, NPY_INT8, &main_y_proc_arr) != 0 ||
      vh_make_1d(n, NPY_INT8, &c_x_proc_arr) != 0 || vh_make_1d(n, NPY_INT8, &c_y_proc_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &stick_x_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &stick_y_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &cstick_y_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &trigger_arr) != 0 ||
      vh_make_1d(n, NPY_UINT16, &pressed_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &lr_timer_arr) != 0 || vh_make_1d(n, NPY_FLOAT32, &light_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &guard_phase_arr) != 0) {
    Py_XDECREF(main_x_proc_arr);
    Py_XDECREF(main_y_proc_arr);
    Py_XDECREF(c_x_proc_arr);
    Py_XDECREF(c_y_proc_arr);
    Py_XDECREF(stick_x_arr);
    Py_XDECREF(stick_y_arr);
    Py_XDECREF(cstick_y_arr);
    Py_XDECREF(trigger_arr);
    Py_XDECREF(pressed_arr);
    Py_XDECREF(lr_timer_arr);
    Py_XDECREF(light_arr);
    Py_XDECREF(guard_phase_arr);
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(buttons_arr);
  const int8_t* main_x = (const int8_t*)PyArray_DATA(main_x_arr);
  const int8_t* main_y = (const int8_t*)PyArray_DATA(main_y_arr);
  const int8_t* c_x = (const int8_t*)PyArray_DATA(c_x_arr);
  const int8_t* c_y = (const int8_t*)PyArray_DATA(c_y_arr);
  const uint8_t* l = (const uint8_t*)PyArray_DATA(l_arr);
  const uint8_t* r = (const uint8_t*)PyArray_DATA(r_arr);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(action_arr);
  const int16_t* frame = (const int16_t*)PyArray_DATA(frame_arr);
  const uint8_t* facing = (const uint8_t*)PyArray_DATA(facing_arr);
  const float* shield = (const float*)PyArray_DATA(shield_arr);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(hitlag_arr);
  const uint8_t* flags = (const uint8_t*)PyArray_DATA(flags_arr);
  const npy_intp flags_cols = PyArray_DIM(flags_arr, 1);
  const uint16_t* neutral = (const uint16_t*)PyArray_DATA(neutral_arr);
  const uint16_t* frame_max = (const uint16_t*)PyArray_DATA(frame_max_arr);
  int8_t* main_x_proc = (int8_t*)PyArray_DATA(main_x_proc_arr);
  int8_t* main_y_proc = (int8_t*)PyArray_DATA(main_y_proc_arr);
  int8_t* c_x_proc = (int8_t*)PyArray_DATA(c_x_proc_arr);
  int8_t* c_y_proc = (int8_t*)PyArray_DATA(c_y_proc_arr);
  float* stick_x = (float*)PyArray_DATA(stick_x_arr);
  float* stick_y = (float*)PyArray_DATA(stick_y_arr);
  float* cstick_y = (float*)PyArray_DATA(cstick_y_arr);
  float* trigger = (float*)PyArray_DATA(trigger_arr);
  uint16_t* pressed = (uint16_t*)PyArray_DATA(pressed_arr);
  uint8_t* lr_timer_out = (uint8_t*)PyArray_DATA(lr_timer_arr);
  float* light_out = (float*)PyArray_DATA(light_arr);
  uint8_t* guard_phase_out = (uint8_t*)PyArray_DATA(guard_phase_arr);

  const float fdz_x = (float)deadzone_x;
  const float fdz_y = (float)deadzone_y;
  const float lerp = (float)guard_lerp;
  const float rad_to_deg = 180.0f / 3.14159265358979323846f;
  const uint16_t mask_lr = (uint16_t)((button_mask_lr | button_mask_z) & 0xFFFFu);
  int guard_init = guard_x10_init;
  if (guard_init < 0) guard_init = 0;
  if (guard_init > 255) guard_init = 255;
  const float hitlag_slope = (float)hitlag_dmg_mul;
  const float hitlag_base_f = (float)hitlag_base;
  int reflect_x14_init = powershield_reflect_frames + 1;
  int reflect_x18_init = powershield_reflect_total_frames + 1;
  if (reflect_x14_init < 0) reflect_x14_init = 0;
  if (reflect_x14_init > 255) reflect_x14_init = 255;
  if (reflect_x18_init < 0) reflect_x18_init = 0;
  if (reflect_x18_init > 255) reflect_x18_init = 255;

  uint16_t prev_buttons = 0u;
  uint16_t guard_x8 = 0u;
  float guard_x4 = 0.0f;
  uint8_t guard_xc = 0u;
  int guard_x10 = 0;
  float light = 0.0f;
  uint8_t lr_timer = 0xFFu;
  bool prev_lr_held = false;
  bool lr_latched = false;
  int guard_x1c = 0;
  int guard_setoff_damage_min = 0;
  int reflect_x14 = 0;
  int reflect_x18 = 0;
  uint8_t reflect_origin = 0u;
  bool prev_in_reflect = false;

  for (npy_intp i = 0; i < n; i++) {
    const MslStickI8 mv = ucf_process_stick_i8(main_x[i], main_y[i], (uint8_t)(ucf_enabled != 0),
                                               (uint8_t)(cardinals != 0));
    const MslStickI8 cv = ucf_process_stick_i8(c_x[i], c_y[i], (uint8_t)(ucf_enabled != 0),
                                               (uint8_t)(cardinals != 0));
    main_x_proc[i] = mv.x;
    main_y_proc[i] = mv.y;
    c_x_proc[i] = cv.x;
    c_y_proc[i] = cv.y;
    stick_x[i] = vh_apply_deadzone(stick_i8_to_unit(mv.x), fdz_x);
    stick_y[i] = vh_apply_deadzone(stick_i8_to_unit(mv.y), fdz_y);
    cstick_y[i] = vh_apply_deadzone(stick_i8_to_unit(cv.y), fdz_y);
    pressed[i] = (uint16_t)(buttons[i] & (uint16_t)~prev_buttons);
    float trig = (float)(l[i] > r[i] ? l[i] : r[i]) / 255.0f;
    if ((buttons[i] & (uint16_t)button_mask_lr) != 0u) trig = 1.0f;
    trigger[i] = trig;

    const int a = (int)action[i];
    const int prev_a = i > 0 ? (int)action[i - 1] : a;
    const bool in_guard = a == act_guard_on || a == act_guard || a == act_guard_reflect;
    const bool prev_in_guard =
        prev_a == act_guard_on || prev_a == act_guard || prev_a == act_guard_reflect;
    const bool in_guard_setoff = a == act_guard_set_off;

    if (a == act_guard_on && frame[i] == 0) {
      guard_x8 = neutral[i];
      guard_x4 = 0.0f;
    }
    if (in_guard) {
      const float facing_dir = facing[i] != 0u ? 1.0f : -1.0f;
      const float x = stick_x[i] * facing_dir;
      const float y = stick_y[i];
      float rad = atan2f(y, x);
      if (rad < 0.0f) rad += 2.0f * 3.14159265358979323846f;
      float deg = rad * rad_to_deg;
      if (deg < 0.0f) deg = 0.0f;
      if (deg > 359.0f) deg = 359.0f;
      const float offset = (float)guard_x8 - (float)neutral[i];
      float delta = deg - offset;
      if (delta > 180.0f) {
        delta -= 360.0f;
      } else if (delta < -180.0f) {
        delta += 360.0f;
      }
      float next_offset = delta * lerp + offset;
      if (next_offset > 360.0f) {
        next_offset -= 360.0f;
      } else if (next_offset < 0.0f) {
        next_offset += 360.0f;
      }
      int next_x8 = (int)((float)neutral[i] + next_offset);
      if (next_x8 < 0) next_x8 = 0;
      if (next_x8 > (int)frame_max[i]) next_x8 = (int)frame_max[i];
      guard_x8 = (uint16_t)next_x8;
      float mag = sqrtf(stick_x[i] * stick_x[i] + stick_y[i] * stick_y[i]);
      if (mag > 1.0f) mag = 1.0f;
      if (mag < 0.0f) mag = 0.0f;
      guard_x4 = lerp * (mag - guard_x4) + guard_x4;
    }

    if ((a == act_guard_on && prev_a != act_guard_on) ||
        (a == act_guard_reflect && prev_a != act_guard_reflect)) {
      guard_xc = 0u;
      guard_x10 = guard_init;
      light = 0.0f;
    }
    if (!in_guard && !in_guard_setoff) {
      guard_xc = 0u;
      guard_x10 = 0;
      light = 0.0f;
    } else if (in_guard && !prev_in_guard && a == act_guard && prev_a != act_guard_set_off) {
      guard_xc = 0u;
      guard_x10 = guard_init;
      light = 0.0f;
    } else if (in_guard_setoff) {
      if (prev_a != act_guard_set_off && !prev_in_guard && guard_x10 == 0) {
        guard_xc = 0u;
        guard_x10 = guard_init;
        const float t = (trig - trigger_dz) / trigger_denom;
        if (t >= 0.0f) light = t > 1.0f ? 1.0f : t;
      }
    } else {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      const bool held = ((buttons[i] & mask_lr) != 0u) || trig >= trigger_dz;
      if (hl_after_prio0 == 0 && shield[i] > 0.0f) {
        const float t = (trig - trigger_dz) / trigger_denom;
        if (t >= 0.0f) light = t > 1.0f ? 1.0f : t;
        if (guard_x10 > 0) guard_x10 -= 1;
        if (!held) guard_xc = 1u;
      }
    }

    const bool lr_held = ((buttons[i] & mask_lr) != 0u) || trig > trigger_dz;
    const bool lr_edge = lr_held && !prev_lr_held;
    if (hitlag[i] > 0u) {
      lr_latched = lr_latched || lr_edge;
    } else {
      lr_latched = lr_edge;
    }
    if (lr_latched) {
      lr_timer = 0u;
    } else if (lr_timer < 0xFFu) {
      lr_timer += 1u;
    }

    if ((a == act_guard_on && prev_a != act_guard_on) ||
        (a == act_guard_reflect && prev_a != act_guard_reflect)) {
      guard_x1c = 0;
    }
    if (a == act_guard_set_off && (flags[(i * flags_cols) + 3] & 0x20u) != 0u) {
      guard_x1c = guard_special_enable_frames;
      if (guard_x1c < 0) guard_x1c = 0;
      if (guard_x1c > 255) guard_x1c = 255;
    } else if (in_guard) {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (hl_after_prio0 == 0 && guard_x1c > 0) guard_x1c -= 1;
    } else if (a == act_guard_off) {
    } else if (a != act_guard_set_off) {
      guard_x1c = 0;
    }

    if (a != act_guard_set_off) {
      guard_setoff_damage_min = 0;
    } else {
      const int prev_af = i > 0 ? (int)frame[i - 1] : 0;
      const int prev_hl = i > 0 ? (int)hitlag[i - 1] : 0;
      const bool segment_entry = i == 0 || prev_a != act_guard_set_off || (int)frame[i] < prev_af ||
                                 (int)hitlag[i] > prev_hl;
      if (segment_entry && hitlag[i] > 0u) {
        guard_setoff_damage_min = 0xFF;
        for (int dmg = 1; dmg < 0xFF; dmg++) {
          const int result = (int)(((float)dmg * hitlag_slope) + hitlag_base_f);
          if (result >= (int)hitlag[i]) {
            guard_setoff_damage_min = dmg;
            break;
          }
        }
      }
    }
    uint8_t phase = 0u;
    if (a == act_guard_set_off) {
      const int cur_hl = (int)hitlag[i];
      const int prev_hl = i > 0 ? (int)hitlag[i - 1] : 0;
      if (cur_hl > 1) {
        phase = 1u;
      } else if (cur_hl == 1) {
        phase = 2u;
      } else if (prev_a == act_guard_set_off && prev_hl > 0) {
        phase = 3u;
      }
    }
    guard_phase_out[i] = phase;
    uint8_t post_hitlag_owner = 0u;
    if (a == act_guard_set_off) {
      if (phase == 2u || phase == 3u) {
        post_hitlag_owner = (flags[(i * flags_cols) + 3] & 0x20u) != 0u ? 2u : 1u;
      }
    }

    const bool in_reflect = a == act_guard_reflect;
    if (!in_reflect) {
      reflect_x14 = 0;
      reflect_x18 = 0;
      reflect_origin = 0u;
    } else if (!prev_in_reflect) {
      reflect_x14 = reflect_x14_init;
      reflect_x18 = reflect_x18_init;
      reflect_origin = (uint8_t)((prev_a == act_guard_on || prev_a == act_guard) ? 1u : 0u);
    } else {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (hl_after_prio0 == 0) {
        if (reflect_x14 > 0) reflect_x14 -= 1;
        if (reflect_x18 > 0) reflect_x18 -= 1;
      }
    }
    prev_in_reflect = in_reflect;

    if (i < n_samples) {
      MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
      seed->guard_tilt_x8[slot] = guard_x8;
      seed->guard_tilt_x4[slot] = guard_x4;
      seed->guard_release_latched_xc[slot] = (uint8_t)(guard_xc ? 1u : 0u);
      seed->guard_x10[slot] = (uint8_t)(guard_x10 & 0xFF);
      seed->lightshield_amount[slot] = light;
      seed->guard_special_enable_timer_x1c[slot] = (uint8_t)(guard_x1c & 0xFF);
      seed->guard_setoff_hitlag_damage_min[slot] = (uint8_t)(guard_setoff_damage_min & 0xFF);
      seed->guard_setoff_hitlag_exit_phase_u8[slot] = phase;
      seed->guard_setoff_post_hitlag_owner_u8[slot] = post_hitlag_owner;
      seed->lr_press_timer[slot] = lr_timer;
      seed->guard_reflect_timer_x14[slot] = (uint8_t)(reflect_x14 & 0xFF);
      seed->guard_reflect_timer_x18[slot] = (uint8_t)(reflect_x18 & 0xFF);
      seed->guard_reflect_origin_guardon_u8[slot] = reflect_origin;
    }
    light_out[i] = light;
    lr_timer_out[i] = lr_timer;
    prev_buttons = buttons[i];
    prev_lr_held = lr_held;
  }

  return Py_BuildValue("NNNNNNNNNNNN", main_x_proc_arr, main_y_proc_arr, c_x_proc_arr, c_y_proc_arr,
                       stick_x_arr, stick_y_arr, cstick_y_arr, trigger_arr, pressed_arr,
                       lr_timer_arr, light_arr, guard_phase_arr);
}

PyObject* msl_validation_derive_input_history_suffix_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject *seed_obj = NULL, *buttons_obj = NULL, *raw_main_x_obj = NULL, *raw_main_y_obj = NULL;
  PyObject *stick_x_obj = NULL, *stick_y_obj = NULL, *cstick_y_obj = NULL, *trigger_obj = NULL;
  PyObject *pressed_obj = NULL, *action_obj = NULL;
  PyObject *frame_obj = NULL, *facing_obj = NULL, *hitlag_obj = NULL, *speed_y_obj = NULL;
  PyObject *ground_obj = NULL, *reset_obj = NULL, *turn_frames_obj = NULL;
  int slot = 0;
  double tilt_x = 0.0, tilt_y = 0.0, fastfall_stick = 0.0, tap_jump = 0.0;
  int fastfall_tilt_max = 0, tap_jump_tilt_max = 0;
  double dash_run_jump_y = 0.0, tap_release = 0.0, dash_flick_abs = 0.0;
  int dash_flick_tilt_max = 0;
  double trigger_min = 0.0;
  int button_mask_a = 0, button_mask_b = 0, button_mask_xy = 0, button_mask_du = 0;
  int button_mask_dd = 0, button_mask_lr = 0, button_mask_z = 0;
  int act_guard_reflect = 0, act_kneebend = 0, act_dash = 0, act_run = 0, act_run_direct = 0;
  int act_run_brake = 0, act_turn_run = 0, act_turn = 0;
  int act_jump_f = 0, act_jump_b = 0, act_jump_air_f = 0, act_jump_air_b = 0;
  int act_fall = 0, act_fall_f = 0, act_fall_b = 0, act_fall_aerial = 0, act_fall_aerial_f = 0;
  int act_fall_aerial_b = 0, act_fall_special = 0, act_fall_special_f = 0, act_fall_special_b = 0;
  int act_damage_fall = 0, act_attack_air_n = 0, act_attack_air_f = 0, act_attack_air_b = 0;
  int act_attack_air_hi = 0, act_attack_air_lw = 0, act_escape_air = 0;
  if (!PyArg_ParseTuple(
          args, "OiOOOOOOOOOOOOOOOdddididddiOdiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", &seed_obj,
          &slot, &buttons_obj, &raw_main_x_obj, &raw_main_y_obj, &stick_x_obj, &stick_y_obj,
          &cstick_y_obj, &trigger_obj, &pressed_obj, &action_obj, &frame_obj, &facing_obj,
          &hitlag_obj, &speed_y_obj, &ground_obj, &reset_obj, &tilt_x, &tilt_y, &fastfall_stick,
          &fastfall_tilt_max, &tap_jump, &tap_jump_tilt_max, &dash_run_jump_y, &tap_release,
          &dash_flick_abs, &dash_flick_tilt_max, &turn_frames_obj, &trigger_min, &button_mask_a,
          &button_mask_b, &button_mask_xy, &button_mask_du, &button_mask_dd, &button_mask_lr,
          &button_mask_z, &act_guard_reflect, &act_kneebend, &act_dash, &act_run, &act_run_direct,
          &act_run_brake, &act_turn_run, &act_turn, &act_jump_f, &act_jump_b, &act_jump_air_f,
          &act_jump_air_b, &act_fall, &act_fall_f, &act_fall_b, &act_fall_aerial,
          &act_fall_aerial_f, &act_fall_aerial_b, &act_fall_special, &act_fall_special_f,
          &act_fall_special_b, &act_damage_fall, &act_attack_air_n, &act_attack_air_f,
          &act_attack_air_b, &act_attack_air_hi, &act_attack_air_lw, &act_escape_air)) {
    return NULL;
  }
  PyArrayObject* seed_arr = vh_require(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* buttons_arr = vh_require(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* raw_main_x_arr = vh_require(raw_main_x_obj, NPY_INT8, 1, "raw_main_x");
  PyArrayObject* raw_main_y_arr = vh_require(raw_main_y_obj, NPY_INT8, 1, "raw_main_y");
  PyArrayObject* sx_arr = vh_require(stick_x_obj, NPY_FLOAT32, 1, "stick_x");
  PyArrayObject* sy_arr = vh_require(stick_y_obj, NPY_FLOAT32, 1, "stick_y");
  PyArrayObject* cy_arr = vh_require(cstick_y_obj, NPY_FLOAT32, 1, "cstick_y");
  PyArrayObject* trig_arr = vh_require(trigger_obj, NPY_FLOAT32, 1, "trigger");
  PyArrayObject* pressed_arr = vh_require(pressed_obj, NPY_UINT16, 1, "buttons_pressed");
  PyArrayObject* action_arr = vh_require(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame_arr = vh_require(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* facing_arr = vh_require(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* hitlag_arr = vh_require(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* speed_y_arr = vh_require(speed_y_obj, NPY_FLOAT32, 1, "speed_y");
  PyArrayObject* ground_arr = vh_require(ground_obj, NPY_UINT8, 1, "on_ground");
  PyArrayObject* reset_arr = vh_require(reset_obj, NPY_BOOL, 1, "reset_mask");
  PyArrayObject* turn_frames_arr = vh_require(turn_frames_obj, NPY_UINT8, 1, "turn_frames");
  if (seed_arr == NULL || buttons_arr == NULL || raw_main_x_arr == NULL || raw_main_y_arr == NULL ||
      sx_arr == NULL || sy_arr == NULL || cy_arr == NULL || trig_arr == NULL ||
      pressed_arr == NULL || action_arr == NULL || frame_arr == NULL || facing_arr == NULL ||
      hitlag_arr == NULL || speed_y_arr == NULL || ground_arr == NULL || reset_arr == NULL ||
      turn_frames_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action_arr);
  const npy_intp n_samples = n - 1;
  if (n < 1) {
    PyErr_SetString(PyExc_ValueError, "action_id must contain at least one frame");
    return NULL;
  }
  if (slot < 0 || slot >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "slot out of range");
    return NULL;
  }
  if (vh_seed_rows(seed_arr, n_samples) != 0 || vh_check_len(buttons_arr, n, "buttons") != 0 ||
      vh_check_len(raw_main_x_arr, n, "raw_main_x") != 0 ||
      vh_check_len(raw_main_y_arr, n, "raw_main_y") != 0 ||
      vh_check_len(sx_arr, n, "stick_x") != 0 || vh_check_len(sy_arr, n, "stick_y") != 0 ||
      vh_check_len(cy_arr, n, "cstick_y") != 0 || vh_check_len(trig_arr, n, "trigger") != 0 ||
      vh_check_len(pressed_arr, n, "buttons_pressed") != 0 ||
      vh_check_len(frame_arr, n, "action_frame") != 0 ||
      vh_check_len(facing_arr, n, "facing") != 0 || vh_check_len(hitlag_arr, n, "hitlag") != 0 ||
      vh_check_len(speed_y_arr, n, "speed_y") != 0 ||
      vh_check_len(ground_arr, n, "on_ground") != 0 ||
      vh_check_len(reset_arr, n, "reset_mask") != 0 ||
      vh_check_len(turn_frames_arr, n, "turn_frames") != 0) {
    return NULL;
  }
  PyArrayObject *tilt_y_pre_arr = NULL, *tilt_y_post_arr = NULL, *fall_fast_arr = NULL;
  PyArrayObject* turn_has_arr = NULL;
  if (vh_make_1d(n, NPY_UINT8, &tilt_y_pre_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &tilt_y_post_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &fall_fast_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &turn_has_arr) != 0) {
    Py_XDECREF(tilt_y_pre_arr);
    Py_XDECREF(tilt_y_post_arr);
    Py_XDECREF(fall_fast_arr);
    Py_XDECREF(turn_has_arr);
    return NULL;
  }
  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(buttons_arr);
  const int8_t* raw_main_x = (const int8_t*)PyArray_DATA(raw_main_x_arr);
  const int8_t* raw_main_y = (const int8_t*)PyArray_DATA(raw_main_y_arr);
  const float* sx = (const float*)PyArray_DATA(sx_arr);
  const float* sy = (const float*)PyArray_DATA(sy_arr);
  const float* cy = (const float*)PyArray_DATA(cy_arr);
  const float* trig = (const float*)PyArray_DATA(trig_arr);
  const uint16_t* pressed = (const uint16_t*)PyArray_DATA(pressed_arr);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(action_arr);
  const int16_t* frame = (const int16_t*)PyArray_DATA(frame_arr);
  const uint8_t* facing = (const uint8_t*)PyArray_DATA(facing_arr);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(hitlag_arr);
  const float* speed_y = (const float*)PyArray_DATA(speed_y_arr);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(ground_arr);
  const npy_bool* reset = (const npy_bool*)PyArray_DATA(reset_arr);
  const uint8_t* turn_frames = (const uint8_t*)PyArray_DATA(turn_frames_arr);
  uint8_t* tilt_y_pre_out = (uint8_t*)PyArray_DATA(tilt_y_pre_arr);
  uint8_t* tilt_y_post_out = (uint8_t*)PyArray_DATA(tilt_y_post_arr);
  uint8_t* fall_fast_out = (uint8_t*)PyArray_DATA(fall_fast_arr);
  uint8_t* turn_has_out = (uint8_t*)PyArray_DATA(turn_has_arr);

  float prev_x = 0.0f, prev_y = 0.0f, prev_trig = 0.0f;
  uint8_t tilt_x_post = 0xFEu, tilt_y_post = 0xFEu, fall_fast_prev = 0u;
  float vy_prev = 0.0f;
  uint8_t ground_prev = n > 0 ? ground[0] : 0u;
  uint8_t x673 = 0xFEu, x676 = 0xFEu, x2228 = 0u, x679 = 0xFEu;
  uint8_t x674 = 0xFEu, x677 = 0xFEu, x67A = 0xFEu;
  uint8_t x675 = 0xFEu, x67B = 0xFEu, x678 = 0xFEu;
  uint8_t x67C = 0xFFu, x67D = 0xFFu, x67E = 0xFFu, x680 = 0xFFu;
  uint8_t x681 = 0xFFu, x682 = 0xFFu, x683 = 0xFFu, x684 = 0xFFu;
  uint16_t x668_latched = 0u;
  uint8_t x672 = 0xFEu;
  uint8_t kb_jump = 0u, kb_short = 0u;
  bool prev_in_kb = false;
  int turn_to = 0, turn_x8 = 0;
  uint8_t turn_has = 0u;
  bool prev_in_turn = false;
  int8_t pad_x[4] = {0, 0, 0, 0};
  int8_t pad_y[4] = {0, 0, 0, 0};
  uint8_t pad_index = 0u, pad_sdrop = 0u;
  const float sdrop_y_thresh = vh_ucf_popo_to_nana(-0.6125f);
  const int sdrop_delta_sq_thresh = 44 * 44;

  for (npy_intp i = 0; i < n; i++) {
    const uint16_t a = action[i];
    const bool is_dash = (int)a == act_dash;
    const bool is_jump_ground = (int)a == act_jump_f || (int)a == act_jump_b;
    const bool is_jump_air = (int)a == act_jump_air_f || (int)a == act_jump_air_b;
    const bool is_jump = is_jump_ground || is_jump_air;
    const uint16_t prev_a = i > 0 ? action[i - 1] : action[i];
    const bool pre_input_jump_entry = is_jump_ground && a != prev_a;
    const bool jump_entry = is_jump_air && a != prev_a;
    const bool fastfall_ok =
        is_jump || (int)a == act_fall || (int)a == act_fall_f || (int)a == act_fall_b ||
        (int)a == act_fall_aerial || (int)a == act_fall_aerial_f || (int)a == act_fall_aerial_b ||
        (int)a == act_fall_special || (int)a == act_fall_special_f ||
        (int)a == act_fall_special_b || (int)a == act_damage_fall || (int)a == act_attack_air_n ||
        (int)a == act_attack_air_f || (int)a == act_attack_air_b || (int)a == act_attack_air_hi ||
        (int)a == act_attack_air_lw || (int)a == act_escape_air;

    uint8_t tx_pre = tilt_x_post;
    if (sx[i] >= (float)tilt_x) {
      tx_pre = prev_x >= (float)tilt_x ? vh_sat_inc_fe(tx_pre) : 0u;
    } else if (sx[i] <= -(float)tilt_x) {
      tx_pre = prev_x <= -(float)tilt_x ? vh_sat_inc_fe(tx_pre) : 0u;
    } else {
      tx_pre = 0xFEu;
    }
    tilt_x_post = tx_pre;
    if (is_dash && (i == 0 || action[i - 1] != action[i])) tilt_x_post = 0xFEu;
    if (reset[i]) tilt_x_post = 0xFEu;

    uint8_t ty_pre = tilt_y_post;
    if (sy[i] >= (float)tilt_y) {
      ty_pre = prev_y >= (float)tilt_y ? vh_sat_inc_fe(ty_pre) : 0u;
    } else if (sy[i] <= -(float)tilt_y) {
      ty_pre = prev_y <= -(float)tilt_y ? vh_sat_inc_fe(ty_pre) : 0u;
    } else {
      ty_pre = 0xFEu;
    }
    uint8_t ff_start = fall_fast_prev;
    uint8_t ty_post = ty_pre;
    if (pre_input_jump_entry) {
      ff_start = 0u;
      if (sy[i] >= (float)tilt_y) {
        ty_post = prev_y >= (float)tilt_y ? 0xFEu : 0u;
      } else if (sy[i] <= -(float)tilt_y) {
        ty_post = prev_y <= -(float)tilt_y ? 0xFEu : 0u;
      } else {
        ty_post = 0xFEu;
      }
    }
    if (jump_entry) {
      ty_post = 0xFEu;
      ff_start = 0u;
    }
    uint8_t ff_after = ff_start;
    if (!ground_prev && fastfall_ok && hitlag[i] <= 1u) {
      if (!ff_start && vy_prev < 0.0f && sy[i] <= -(float)fastfall_stick &&
          (int)ty_post < fastfall_tilt_max) {
        ff_after = 1u;
        ty_post = 0xFEu;
      }
    }
    if (reset[i]) ty_post = 0xFEu;
    const uint8_t ff_post = ground[i] ? 0u : ff_after;
    tilt_y_pre_out[i] = ty_pre;
    tilt_y_post_out[i] = ty_post;
    fall_fast_out[i] = ff_post;

    pad_index = (uint8_t)((pad_index + 1u) & 3u);
    pad_x[pad_index] = raw_main_x[i];
    pad_y[pad_index] = raw_main_y[i];
    const int ix = (int)truncf(fabsf(sx[i]) * 80.0f - 0.0001f) + 2;
    const int iy = (int)truncf(fabsf(sy[i]) * 80.0f - 0.0001f) + 2;
    const bool is_rim = (ix * ix + iy * iy) > 80 * 80;
    const int16_t prev2_y = (i >= 2) ? (int16_t)pad_y[(pad_index + 2u) & 3u] : 0;
    const int dy = (int)((int16_t)pad_y[pad_index] - prev2_y);
    if (sy[i] > sdrop_y_thresh || !is_rim) {
      pad_sdrop = 0u;
    } else if (pad_sdrop != 0u) {
      pad_sdrop = (uint8_t)(pad_sdrop + 1u);
    } else if (ty_pre < 2u && dy * dy > sdrop_delta_sq_thresh) {
      pad_sdrop = 1u;
    } else {
      pad_sdrop = 0u;
    }

    x676 = vh_sat_inc_fe(x676);
    if (sx[i] >= (float)tilt_x) {
      if (prev_x >= (float)tilt_x) {
        x673 = vh_sat_inc_fe(x673);
        x679 = vh_sat_inc_fe(x679);
      } else {
        x676 = x673 = 0u;
        x2228 = 1u;
      }
    } else if (sx[i] <= -(float)tilt_x) {
      if (prev_x <= -(float)tilt_x) {
        x673 = vh_sat_inc_fe(x673);
        x679 = vh_sat_inc_fe(x679);
      } else {
        x676 = x673 = 0u;
        x2228 = 0u;
      }
    } else {
      x679 = x673 = 0xFEu;
    }
    x677 = vh_sat_inc_fe(x677);
    if (sy[i] >= (float)tilt_y) {
      if (prev_y >= (float)tilt_y) {
        x674 = vh_sat_inc_fe(x674);
        x67A = vh_sat_inc_fe(x67A);
      } else {
        x677 = x674 = 0u;
      }
    } else if (sy[i] <= -(float)tilt_y) {
      if (prev_y <= -(float)tilt_y) {
        x674 = vh_sat_inc_fe(x674);
        x67A = vh_sat_inc_fe(x67A);
      } else {
        x677 = x674 = 0u;
      }
    } else {
      x67A = x674 = 0xFEu;
    }
    if (vh_lb_8000d148(prev_x, prev_y, sx[i], sy[i], 0.0f, 0.0f, (float)tilt_x)) {
      x67A = 0u;
      x679 = 0u;
    }
    x678 = vh_sat_inc_fe(x678);
    if (trig[i] >= (float)trigger_min) {
      if (prev_trig >= (float)trigger_min) {
        x675 = vh_sat_inc_fe(x675);
        x67B = vh_sat_inc_fe(x67B);
      } else {
        x67B = x678 = x675 = 0u;
      }
    } else {
      x67B = x675 = 0xFEu;
    }
    uint16_t raw = pressed[i];
    if ((raw & (uint16_t)button_mask_z) != 0u) raw = (uint16_t)(raw | (uint16_t)button_mask_a);
    uint16_t bpi = raw;
    if (hitlag[i] > 0u) {
      x668_latched = (uint16_t)(x668_latched | raw);
      bpi = x668_latched;
    } else {
      x668_latched = 0u;
    }
    if ((bpi & (uint16_t)button_mask_a) != 0u) {
      x683 = x67C;
      x67C = 0u;
    } else {
      x67C = vh_sat_inc_ff(x67C);
    }
    if ((bpi & (uint16_t)button_mask_b) != 0u)
      x67D = 0u;
    else
      x67D = vh_sat_inc_ff(x67D);
    if ((bpi & (uint16_t)button_mask_xy) != 0u)
      x67E = 0u;
    else
      x67E = vh_sat_inc_ff(x67E);
    if ((bpi & (uint16_t)button_mask_du) != 0u)
      x681 = 0u;
    else
      x681 = vh_sat_inc_ff(x681);
    if ((bpi & (uint16_t)button_mask_dd) != 0u)
      x682 = 0u;
    else
      x682 = vh_sat_inc_ff(x682);
    if ((bpi & (uint16_t)button_mask_lr) != 0u) {
      x684 = x680;
      x680 = 0u;
    } else {
      x680 = vh_sat_inc_ff(x680);
    }
    uint8_t x672_pre = x672;
    if (trig[i] >= (float)trigger_min) {
      x672_pre = prev_trig >= (float)trigger_min ? vh_sat_inc_fe(x672_pre) : 0u;
    } else {
      x672_pre = 0xFEu;
    }
    x672 = ((int)a == act_guard_reflect && (i == 0 || (int)prev_a != act_guard_reflect)) ? 0xFEu
                                                                                         : x672_pre;

    const bool cur_kb = (int)a == act_kneebend;
    if (!cur_kb) {
      kb_jump = 0u;
      kb_short = 0u;
      prev_in_kb = false;
    } else {
      if (!prev_in_kb) {
        const bool dash_src = (int)prev_a == act_dash || (int)prev_a == act_run ||
                              (int)prev_a == act_run_direct || (int)prev_a == act_run_brake ||
                              (int)prev_a == act_turn_run;
        kb_jump = 0u;
        if (dash_src) {
          if ((pressed[i] & (uint16_t)button_mask_xy) != 0u)
            kb_jump = 3u;
          else if (sy[i] >= (float)dash_run_jump_y && (int)ty_pre < tap_jump_tilt_max)
            kb_jump = 1u;
          else if (cy[i] >= (float)tap_jump)
            kb_jump = 2u;
        } else {
          if (sy[i] >= (float)tap_jump && (int)ty_pre < tap_jump_tilt_max)
            kb_jump = 1u;
          else if ((pressed[i] & (uint16_t)button_mask_xy) != 0u)
            kb_jump = 3u;
          else if (cy[i] >= (float)tap_jump)
            kb_jump = 2u;
        }
        kb_short = 0u;
      }
      if (!kb_short) {
        if (kb_jump == 3u) {
          if ((buttons[i] & (uint16_t)button_mask_xy) == 0u) kb_short = 1u;
        } else if (kb_jump == 1u) {
          if (sy[i] < (float)tap_release) kb_short = 1u;
        } else if (kb_jump == 2u) {
          if (cy[i] < (float)tap_release) kb_short = 1u;
        }
      }
      prev_in_kb = true;
    }

    const bool cur_turn = (int)a == act_turn;
    if (!cur_turn) {
      turn_to = 0;
      turn_has = 0u;
      turn_x8 = 0;
      prev_in_turn = false;
    } else {
      const bool restart = prev_in_turn && i > 0 && frame[i] < frame[i - 1];
      if (!prev_in_turn || restart) {
        bool is_smash = false;
        float facing_dir = 1.0f;
        if (i > 0) {
          facing_dir = facing[i - 1] != 0u ? 1.0f : -1.0f;
          if (fabsf(sx[i]) >= (float)dash_flick_abs && (int)tilt_x_post < dash_flick_tilt_max &&
              sx[i] * facing_dir < 0.0f) {
            is_smash = true;
          }
        }
        turn_to = is_smash ? 0 : (int)turn_frames[i];
        if (turn_to < 0) turn_to = 0;
        if (turn_to > 0xFE) turn_to = 0xFE;
        turn_has = 0u;
        turn_x8 = is_smash ? (facing_dir > 0.0f ? 1 : -1) : 0;
      } else {
        if (turn_to > 0) {
          turn_to -= 1;
        } else if (!turn_has) {
          turn_has = 1u;
        }
        const float facing_dir_i = facing[i] != 0u ? 1.0f : -1.0f;
        const float facing_after = turn_has ? facing_dir_i : -facing_dir_i;
        if (sx[i] * facing_after >= (float)dash_flick_abs &&
            (int)tilt_x_post < dash_flick_tilt_max) {
          turn_x8 = facing_after > 0.0f ? 1 : -1;
        }
      }
      prev_in_turn = true;
    }
    turn_has_out[i] = turn_has;

    if (i < n_samples) {
      MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
      seed->tilt_timer_x[slot] = tilt_x_post;
      seed->tilt_timer_y[slot] = ty_post;
      seed->fall_fast[slot] = ff_post;
      seed->fall_fast_hitlag_exit_owner[slot] =
          (uint8_t)((hitlag[i] == 1u && fastfall_ok && ff_post != 0u) ? 1u : 0u);
      seed->ucf_padbuf_index[slot] = pad_index;
      seed->ucf_padbuf_sdrop_up_frames[slot] = pad_sdrop;
      for (int k = 0; k < 4; k++) {
        seed->ucf_padbuf_stick_x[slot][k] = pad_x[k];
        seed->ucf_padbuf_stick_y[slot][k] = pad_y[k];
      }
      seed->x672_input_timer[slot] = x672;
      seed->x673[slot] = x673;
      seed->x674[slot] = x674;
      seed->x675[slot] = x675;
      seed->x676_x[slot] = x676;
      seed->x2228_b7[slot] = x2228;
      seed->x677_y[slot] = x677;
      seed->x678[slot] = x678;
      seed->x679_x[slot] = x679;
      seed->x67A_y[slot] = x67A;
      seed->x67B[slot] = x67B;
      seed->x67C[slot] = x67C;
      seed->x67D[slot] = x67D;
      seed->x67E[slot] = x67E;
      seed->x680[slot] = x680;
      seed->x681[slot] = x681;
      seed->x682[slot] = x682;
      seed->x683[slot] = x683;
      seed->x684[slot] = x684;
      seed->kneebend_jump_input[slot] = kb_jump;
      seed->kneebend_is_short_hop[slot] = kb_short;
      seed->turn_frames_to_turn[slot] = (uint8_t)(turn_to & 0xFF);
      seed->turn_has_turned[slot] = turn_has;
      seed->turn_x8[slot] = (int8_t)turn_x8;
    }
    prev_x = sx[i];
    prev_y = sy[i];
    prev_trig = trig[i];
    tilt_y_post = ty_post;
    fall_fast_prev = ff_post;
    vy_prev = speed_y[i];
    ground_prev = ground[i];
  }
  return Py_BuildValue("NNNN", tilt_y_pre_arr, tilt_y_post_arr, fall_fast_arr, turn_has_arr);
}
