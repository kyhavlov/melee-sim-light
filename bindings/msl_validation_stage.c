/* Native validation stage-lane materialization. */

#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY

#include "msl_validation_stage.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/api.h"
#include "../src/batch_internal.h"
#include "../src/ids.h"
#include "../src/stage_item_params.h"

static int require_u8_rows(PyArrayObject* arr, npy_intp rows, npy_intp min_cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s must be uint8[%zd, >=%zd]", name, rows, min_cols);
    return -1;
  }
  return 0;
}

typedef struct ShyguyKeyState {
  bool used;
  uint32_t spawn_id;
  uint32_t key_id;
  int prev_state;
  int state_age;
  int hitlag;
  int state3_moving_age;
  int state4_zero_x_prefix;
  int active_turn_delay;
  int prev_damage;
  float prev_vel_x;
  bool has_prev_vel_x;
} ShyguyKeyState;

typedef struct ShyguyPhaseState {
  bool used;
  uint32_t spawn_id;
  uint32_t key_id;
  int state;
  uint8_t phase;
  float prev_vel_y;
  bool has_prev_vel_y;
} ShyguyPhaseState;

typedef struct ShyguySpawnState {
  bool used;
  uint32_t spawn_id;
  int first_seen;
  uint32_t group_base;
} ShyguySpawnState;

typedef struct ShyguyGroupState {
  bool used;
  uint32_t group_base;
  uint8_t speed_index;
  bool speed_valid;
} ShyguyGroupState;

typedef struct ShyguyPrevItem {
  uint32_t spawn_id;
  uint16_t instance_id;
  float vel_y;
} ShyguyPrevItem;

static float shyguy_phase_value(const float* dyn, int phase) {
  phase &= 0xFF;
  if (phase == 0) return 0.0f;
  if (phase <= 128) return dyn[phase - 1];
  return dyn[256 - phase];
}

static float shyguy_rate2_value(const float* dyn, int phase) {
  phase &= 0xFF;
  if (phase == 0) return 0.0f;
  if (phase == 1) return shyguy_phase_value(dyn, 1);
  const int base = (2 * phase) - 2;
  return shyguy_phase_value(dyn, base) + shyguy_phase_value(dyn, base + 1);
}

static float shyguy_pair_forward(const float* dyn, int pair) {
  pair &= 0x3F;
  const int base = pair * 2;
  return dyn[base] + dyn[(base + 1) & 0x7F];
}

static float shyguy_current_vel_for_phase(const float* dyn, int phase, int state) {
  phase &= 0xFF;
  if (state == 4) {
    if (phase <= 29) return 0.0f;
    const int q = (phase - 18) & 0xFF;
    if (q == 12) return shyguy_phase_value(dyn, 1);
    if (q == 140) return -shyguy_phase_value(dyn, 1);
    if (q >= 141) return shyguy_pair_forward(dyn, q - 141);
    return shyguy_rate2_value(dyn, q - 11);
  }
  return shyguy_phase_value(dyn, phase);
}

static int shyguy_phase_distance(int a, int b) {
  a &= 0xFF;
  b &= 0xFF;
  const int ab = (a - b) & 0xFF;
  const int ba = (b - a) & 0xFF;
  return ab < ba ? ab : ba;
}

static int shyguy_visible_phase(const float* dyn, float prev_vel_y, float cur_vel_y, int state,
                                int predicted) {
  const float eps = 0.001f;
  if (state == 4 && fabsf(cur_vel_y) <= eps) return -1;
  int best = -1;
  int best_dist = 999;
  for (int phase = 0; phase < 256; phase++) {
    const int prev = (phase - 1) & 0xFF;
    if (fabsf(shyguy_current_vel_for_phase(dyn, phase, state) - cur_vel_y) <= eps &&
        fabsf(shyguy_current_vel_for_phase(dyn, prev, state) - prev_vel_y) <= eps) {
      const int dist = shyguy_phase_distance(phase, predicted);
      if (best < 0 || dist < best_dist) {
        best = phase;
        best_dist = dist;
      }
    }
  }
  return best;
}

static ShyguyKeyState* shyguy_key_state(ShyguyKeyState* states, size_t* count, size_t cap,
                                        uint32_t spawn_id, uint32_t key_id) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id && states[i].key_id == key_id) {
      return &states[i];
    }
  }
  if (*count >= cap) return NULL;
  ShyguyKeyState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->key_id = key_id;
  st->prev_state = -1;
  (*count)++;
  return st;
}

static ShyguyPhaseState* shyguy_phase_state(ShyguyPhaseState* states, size_t* count, size_t cap,
                                            uint32_t spawn_id, uint32_t key_id) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id && states[i].key_id == key_id) {
      return &states[i];
    }
  }
  if (*count >= cap) return NULL;
  ShyguyPhaseState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->key_id = key_id;
  st->state = -1;
  (*count)++;
  return st;
}

static ShyguySpawnState* shyguy_spawn_state(ShyguySpawnState* states, size_t* count, size_t cap,
                                            uint32_t spawn_id, int fi) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id) return &states[i];
  }
  if (*count >= cap) return NULL;
  ShyguySpawnState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->first_seen = fi;
  st->group_base = spawn_id;
  (*count)++;
  return st;
}

static ShyguyGroupState* shyguy_group_state(ShyguyGroupState* states, size_t* count, size_t cap,
                                            uint32_t group_base) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].group_base == group_base) return &states[i];
  }
  if (*count >= cap) return NULL;
  ShyguyGroupState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->group_base = group_base;
  (*count)++;
  return st;
}

static int shyguy_pattern_from_item(float pos_x, float pos_y, const float* vpos) {
  int best = pos_x < 0.0f ? 0 : 3;
  const int end = best + 3;
  float best_err = fabsf(pos_y - vpos[best]);
  for (int i = best + 1; i < end; i++) {
    const float err = fabsf(pos_y - vpos[i]);
    if (err < best_err) {
      best = i;
      best_err = err;
    }
  }
  return best;
}

static int shyguy_speed_index_from_item(float vel_x, int state, const float* speeds,
                                        float state4_mul) {
  float speed = fabsf(vel_x);
  if (state == 4 && state4_mul != 0.0f) speed /= state4_mul;
  int best = 0;
  float best_err = fabsf(speed - speeds[0]);
  for (int i = 1; i < MSL_YOSHI_SHYGUY_SPEED_COUNT; i++) {
    const float err = fabsf(speed - speeds[i]);
    if (err < best_err) {
      best = i;
      best_err = err;
    }
  }
  return best;
}

static int shyguy_hitlag_from_damage(uint16_t damage, float damage_mul, float base) {
  const int frames = (int)(((float)damage * damage_mul) + base);
  return frames > 0 ? frames - 1 : 0;
}

PyObject* msl_validation_derive_yoshi_shyguy_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* frame_rng_obj = NULL;
  PyObject* vpos_obj = NULL;
  PyObject* speeds_obj = NULL;
  PyObject* dyn_obj = NULL;
  int replay_stage_id = 0;
  int param_stage_id = 0;
  int item_kind = 0;
  int timer_reset = 0;
  int spawn_delay_step = 0;
  double state4_speed_mul_d = 0.0;
  double hitlag_damage_mul_d = 0.0;
  double hitlag_base_d = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOiiiiiddd", &seed_obj, &items_obj, &frame_rng_obj, &vpos_obj,
                        &speeds_obj, &dyn_obj, &replay_stage_id, &param_stage_id, &item_kind,
                        &timer_reset, &spawn_delay_step, &state4_speed_mul_d, &hitlag_damage_mul_d,
                        &hitlag_base_d)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* items_arr = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* frame_rng =
      require_contiguous_array_readonly(frame_rng_obj, NPY_UINT32, 1, "frame_rng_u32");
  PyArrayObject* vpos_arr = require_contiguous_array_readonly(vpos_obj, NPY_FLOAT32, 1, "vpos");
  PyArrayObject* speeds_arr =
      require_contiguous_array_readonly(speeds_obj, NPY_FLOAT32, 1, "speeds");
  PyArrayObject* dyn_arr = require_contiguous_array_readonly(dyn_obj, NPY_FLOAT32, 1, "dyn_y");
  if (seed_arr == NULL || items_arr == NULL || frame_rng == NULL || vpos_arr == NULL ||
      speeds_arr == NULL || dyn_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  const size_t items_bytes = sizeof(((MslSeed*)0)->items);
  if (n < 0 || PyArray_DIM(items_arr, 0) < n ||
      PyArray_DIM(items_arr, 1) != (npy_intp)items_bytes || PyArray_DIM(frame_rng, 0) < n + 1 ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      PyArray_SIZE(vpos_arr) < MSL_YOSHI_SHYGUY_VPOS_COUNT ||
      PyArray_SIZE(speeds_arr) < MSL_YOSHI_SHYGUY_SPEED_COUNT ||
      PyArray_SIZE(dyn_arr) < MSL_YOSHI_SHYGUY_DYN_Y_COUNT) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "Yoshi Shy Guy validation buffers are invalid");
    }
    return NULL;
  }

  uint8_t* seed_data = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* items_data = (const uint8_t*)PyArray_DATA(items_arr);
  const uint32_t* frame_rng_data = (const uint32_t*)PyArray_DATA(frame_rng);
  const float* vpos = (const float*)PyArray_DATA(vpos_arr);
  const float* speeds = (const float*)PyArray_DATA(speeds_arr);
  const float* dyn = (const float*)PyArray_DATA(dyn_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items_arr, 0);

  const size_t cap = (size_t)(n * MSL_MAX_ITEMS + 16);
  ShyguyKeyState* key_states = (ShyguyKeyState*)calloc(cap, sizeof(*key_states));
  ShyguyPhaseState* phase_states = (ShyguyPhaseState*)calloc(cap, sizeof(*phase_states));
  ShyguySpawnState* spawn_states = (ShyguySpawnState*)calloc(cap, sizeof(*spawn_states));
  ShyguyGroupState* group_states = (ShyguyGroupState*)calloc(cap, sizeof(*group_states));
  ShyguyPrevItem* prev_items = (ShyguyPrevItem*)calloc(MSL_MAX_ITEMS, sizeof(*prev_items));
  int* shyguy_slots = (int*)calloc(MSL_MAX_ITEMS, sizeof(*shyguy_slots));
  uint32_t* new_spawns = (uint32_t*)calloc(MSL_MAX_ITEMS, sizeof(*new_spawns));
  uint8_t* rng_owner = (uint8_t*)calloc((size_t)n, sizeof(*rng_owner));
  if (key_states == NULL || phase_states == NULL || spawn_states == NULL || group_states == NULL ||
      prev_items == NULL || shyguy_slots == NULL || new_spawns == NULL || rng_owner == NULL) {
    free(key_states);
    free(phase_states);
    free(spawn_states);
    free(group_states);
    free(prev_items);
    free(shyguy_slots);
    free(new_spawns);
    free(rng_owner);
    PyErr_NoMemory();
    return NULL;
  }

  size_t key_count = 0;
  size_t phase_count = 0;
  size_t spawn_count = 0;
  size_t group_count = 0;
  int prev_item_count = 0;
  int cur_timer = timer_reset > 0 ? timer_reset - 1 : 0;
  int cur_pattern = 0;
  const bool stage_ok = replay_stage_id == param_stage_id;
  const float state4_speed_mul = (float)state4_speed_mul_d;
  const float hitlag_damage_mul = (float)hitlag_damage_mul_d;
  const float hitlag_base = (float)hitlag_base_d;

  for (npy_intp fi = 0; fi < n; fi++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_data + (size_t)fi * seed_stride);
    const MslItem* items = (const MslItem*)(const void*)(items_data + (size_t)fi * items_stride);
    seed->stage_yoshi_shyguy_timer_u16 = 0u;
    seed->stage_yoshi_shyguy_pattern_u8 = 0u;
    seed->stage_yoshi_shyguy_valid_u8 = 0u;
    seed->stage_yoshi_shyguy_spawn_rng_seed_u32 = 0u;
    seed->stage_yoshi_shyguy_spawn_rng_seed_valid_u8 = 0u;
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      seed->item_shyguy_prev_vel_y[slot] = 0.0f;
      seed->item_shyguy_prev_vel_y_valid[slot] = 0u;
      seed->item_shyguy_dyn_y_phase_u8[slot] = 0u;
      seed->item_shyguy_dyn_y_phase_valid_u8[slot] = 0u;
      seed->item_shyguy_speed_index_u8[slot] = 0u;
      seed->item_shyguy_speed_index_valid_u8[slot] = 0u;
      seed->item_shyguy_delay_u16[slot] = 0u;
      seed->item_shyguy_delay_valid_u8[slot] = 0u;
      seed->item_shyguy_hitlag_u8[slot] = 0u;
      seed->item_shyguy_hitlag_valid_u8[slot] = 0u;
    }

    int shyguy_count = 0;
    int new_count = 0;
    uint32_t group_base = UINT32_MAX;
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      const MslItem* item = &items[slot];
      if (item->exists != 0u && item->type == (uint16_t)item_kind && item->owner == -1) {
        shyguy_slots[shyguy_count++] = slot;
        bool seen_spawn = false;
        for (size_t si = 0; si < spawn_count; si++) {
          if (spawn_states[si].used && spawn_states[si].spawn_id == item->spawn_id) {
            seen_spawn = true;
            break;
          }
        }
        if (!seen_spawn) {
          new_spawns[new_count++] = item->spawn_id;
          if (item->spawn_id < group_base) group_base = item->spawn_id;
        }
      }
    }

    if (stage_ok) {
      seed->stage_yoshi_shyguy_timer_u16 = (uint16_t)(cur_timer < 0 ? 0 : cur_timer);
      seed->stage_yoshi_shyguy_pattern_u8 = (uint8_t)cur_pattern;
      seed->stage_yoshi_shyguy_valid_u8 = 1u;
    }

    for (int si = 0; si < shyguy_count; si++) {
      const int slot = shyguy_slots[si];
      const MslItem* item = &items[slot];
      for (int pi = 0; pi < prev_item_count; pi++) {
        const bool match = item->spawn_id != 0u ? prev_items[pi].spawn_id == item->spawn_id
                                                : prev_items[pi].instance_id == item->instance_id;
        if (match && (item->state == 1u || item->state == 4u)) {
          seed->item_shyguy_prev_vel_y[slot] = prev_items[pi].vel_y;
          seed->item_shyguy_prev_vel_y_valid[slot] = 1u;
          break;
        }
      }
    }

    if (stage_ok && shyguy_count > 0) {
      cur_timer = timer_reset;
      if (new_count > 0) {
        int pattern_slot = shyguy_slots[0];
        for (int si = 0; si < shyguy_count; si++) {
          const int slot = shyguy_slots[si];
          if (items[slot].spawn_id == group_base) {
            pattern_slot = slot;
            break;
          }
        }
        cur_pattern =
            shyguy_pattern_from_item(items[pattern_slot].pos_x, items[pattern_slot].pos_y, vpos);
        seed->stage_yoshi_shyguy_pattern_u8 = (uint8_t)cur_pattern;
        for (int ni = 0; ni < new_count; ni++) {
          ShyguySpawnState* ss =
              shyguy_spawn_state(spawn_states, &spawn_count, cap, new_spawns[ni], (int)fi);
          if (ss == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
            goto fail;
          }
          ss->group_base = group_base;
        }
      }

      for (int si = 0; si < shyguy_count; si++) {
        const int slot = shyguy_slots[si];
        const MslItem* item = &items[slot];
        ShyguySpawnState* ss =
            shyguy_spawn_state(spawn_states, &spawn_count, cap, item->spawn_id, (int)fi);
        if (ss == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
          goto fail;
        }
        if ((item->state == 1u || item->state == 4u) &&
            fabsf(item->vel_x) > MSL_YOSHI_SHYGUY_VISIBLE_TURN_MIN_ABS_VX) {
          ShyguyGroupState* gs =
              shyguy_group_state(group_states, &group_count, cap, ss->group_base);
          if (gs == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy group-state capacity exhausted");
            goto fail;
          }
          gs->speed_index = (uint8_t)shyguy_speed_index_from_item(item->vel_x, item->state, speeds,
                                                                  state4_speed_mul);
          gs->speed_valid = true;
        }
      }

      for (int si = 0; si < shyguy_count; si++) {
        const int slot = shyguy_slots[si];
        const MslItem* item = &items[slot];
        ShyguySpawnState* ss =
            shyguy_spawn_state(spawn_states, &spawn_count, cap, item->spawn_id, (int)fi);
        if (ss == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
          goto fail;
        }
        ShyguyGroupState* gs = shyguy_group_state(group_states, &group_count, cap, ss->group_base);
        if (gs == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy group-state capacity exhausted");
          goto fail;
        }
        if (gs->speed_valid) {
          seed->item_shyguy_speed_index_u8[slot] = gs->speed_index;
          seed->item_shyguy_speed_index_valid_u8[slot] = 1u;
        }

        const uint32_t key_id = item->spawn_id == 0u ? (uint32_t)item->instance_id : item->spawn_id;
        ShyguyKeyState* ks = shyguy_key_state(key_states, &key_count, cap, item->spawn_id, key_id);
        if (ks == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy key-state capacity exhausted");
          goto fail;
        }
        int age = 0;
        const bool damage_callback_reset =
            (ks->prev_state == (int)item->state && (item->state == 2u || item->state == 3u) &&
             (int)item->damage > ks->prev_damage);
        if (ks->prev_state < 0 || ks->prev_state != (int)item->state || damage_callback_reset) {
          age = 0;
          if ((item->state == 2u || item->state == 3u) && item->damage > 0u) {
            int damage_for_hitlag = (int)item->damage;
            if (damage_callback_reset) {
              damage_for_hitlag -= ks->prev_damage;
              if (damage_for_hitlag <= 0) damage_for_hitlag = (int)item->damage;
            }
            ks->hitlag = shyguy_hitlag_from_damage((uint16_t)damage_for_hitlag, hitlag_damage_mul,
                                                   hitlag_base);
          } else {
            ks->hitlag = 0;
          }
          ks->state3_moving_age = 0;
          ks->state4_zero_x_prefix = (item->state == 4u && fabsf(item->vel_x) <= 0.001f) ? 20 : 0;
          ks->active_turn_delay = 0;
        } else {
          age = ks->state_age + 1;
        }

        const int arg0 =
            item->spawn_id >= ss->group_base ? (int)(item->spawn_id - ss->group_base) : 0;
        if (item->state == 0u) {
          age = (int)fi - ss->first_seen;
          int remaining = (spawn_delay_step * arg0) - age - 1;
          if (remaining < 0) remaining = 0;
          seed->item_shyguy_delay_u16[slot] = (uint16_t)remaining;
          seed->item_shyguy_delay_valid_u8[slot] = 1u;
        } else if (item->state == 3u) {
          int rem_hitlag = ks->hitlag;
          if (rem_hitlag < 0) rem_hitlag = 0;
          if (rem_hitlag > 255) rem_hitlag = 255;
          seed->item_shyguy_hitlag_u8[slot] = (uint8_t)rem_hitlag;
          seed->item_shyguy_hitlag_valid_u8[slot] = 1u;
          int remaining = 12 - ks->state3_moving_age;
          if (remaining < 0) remaining = 0;
          seed->item_shyguy_delay_u16[slot] = (uint16_t)remaining;
          seed->item_shyguy_delay_valid_u8[slot] = 1u;
          if (ks->hitlag > 0) {
            ks->hitlag--;
          } else {
            ks->state3_moving_age++;
          }
        } else if (item->state == 2u) {
          int rem_hitlag = ks->hitlag;
          if (rem_hitlag < 0) rem_hitlag = 0;
          if (rem_hitlag > 255) rem_hitlag = 255;
          seed->item_shyguy_hitlag_u8[slot] = (uint8_t)rem_hitlag;
          seed->item_shyguy_hitlag_valid_u8[slot] = 1u;
          if (ks->hitlag > 0) ks->hitlag--;
        } else if (item->state == 4u || item->state == 1u) {
          if (ks->has_prev_vel_x &&
              fabsf(ks->prev_vel_x) > MSL_YOSHI_SHYGUY_VISIBLE_TURN_MIN_ABS_VX &&
              fabsf(item->vel_x) > MSL_YOSHI_SHYGUY_VISIBLE_TURN_MIN_ABS_VX &&
              ((ks->prev_vel_x < 0.0f && item->vel_x > 0.0f) ||
               (ks->prev_vel_x > 0.0f && item->vel_x < 0.0f))) {
            ks->active_turn_delay = (int)MSL_YOSHI_SHYGUY_TURN_DELAY_FRAMES;
          }
          int rem_turn = ks->active_turn_delay;
          if (item->state == 4u && ks->state4_zero_x_prefix > rem_turn) {
            rem_turn = ks->state4_zero_x_prefix;
          }
          if (rem_turn < 0) rem_turn = 0;
          seed->item_shyguy_delay_u16[slot] = (uint16_t)rem_turn;
          seed->item_shyguy_delay_valid_u8[slot] = 1u;
          if (item->state == 4u && ks->state4_zero_x_prefix > 0) ks->state4_zero_x_prefix--;
          if (ks->active_turn_delay > 0) ks->active_turn_delay--;
          seed->item_shyguy_hitlag_u8[slot] = 0u;
          seed->item_shyguy_hitlag_valid_u8[slot] = 1u;
        }
        ks->prev_state = (int)item->state;
        ks->state_age = age;
        ks->prev_damage = (int)item->damage;
        if (item->state == 1u || item->state == 4u) {
          ks->prev_vel_x = item->vel_x;
          ks->has_prev_vel_x = true;
        } else {
          ks->has_prev_vel_x = false;
        }
      }
    } else if (stage_ok && cur_timer > 0) {
      cur_timer--;
    }

    for (int si = 0; si < shyguy_count; si++) {
      const int slot = shyguy_slots[si];
      const MslItem* item = &items[slot];
      if (item->state != 1u && item->state != 4u) continue;
      const uint32_t key_id = item->spawn_id == 0u ? (uint32_t)item->instance_id : item->spawn_id;
      ShyguyPhaseState* ps =
          shyguy_phase_state(phase_states, &phase_count, cap, item->spawn_id, key_id);
      if (ps == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy phase-state capacity exhausted");
        goto fail;
      }
      uint8_t cur_phase = 0u;
      if (ps->state == (int)item->state) {
        const int predicted = ((int)ps->phase + 1) & 0xFF;
        int visible = -1;
        if (item->state == 1u && fabsf(item->vel_x) <= 0.001f && fabsf(item->vel_y) <= 0.001f) {
          visible = 0;
        } else if (item->state == 1u || fabsf(item->vel_x) >= 0.5f) {
          visible = shyguy_visible_phase(dyn, ps->has_prev_vel_y ? ps->prev_vel_y : 0.0f,
                                         item->vel_y, item->state, predicted);
        }
        cur_phase = (uint8_t)(visible < 0 ? predicted : visible);
      }
      seed->item_shyguy_dyn_y_phase_u8[slot] = cur_phase;
      seed->item_shyguy_dyn_y_phase_valid_u8[slot] = 1u;
      ps->state = (int)item->state;
      ps->phase = cur_phase;
      ps->prev_vel_y = item->vel_y;
      ps->has_prev_vel_y = true;
    }

    bool live_seed_shyguy = false;
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      if (seed->items[slot].exists != 0u && seed->items[slot].type == (uint16_t)item_kind) {
        live_seed_shyguy = true;
        break;
      }
    }
    if (seed->stage_yoshi_shyguy_valid_u8 != 0u && !live_seed_shyguy) {
      rng_owner[fi] = 1u;
      if (fi + 1 < PyArray_DIM(frame_rng, 0)) {
        seed->frame_pre_random_seed = frame_rng_data[fi + 1];
      }
    }

    prev_item_count = 0;
    for (int si = 0; si < shyguy_count && prev_item_count < MSL_MAX_ITEMS; si++) {
      const int slot = shyguy_slots[si];
      const MslItem* item = &items[slot];
      if (item->state == 1u || item->state == 4u) {
        prev_items[prev_item_count].spawn_id = item->spawn_id;
        prev_items[prev_item_count].instance_id = item->instance_id;
        prev_items[prev_item_count].vel_y = item->vel_y;
        prev_item_count++;
      }
    }
  }

  for (npy_intp fi = 0; fi < n; fi++) {
    if (rng_owner[fi] == 0u) continue;
    MslSeed* seed = (MslSeed*)(void*)(seed_data + (size_t)fi * seed_stride);
    const npy_intp spawn_idx = fi + (npy_intp)seed->stage_yoshi_shyguy_timer_u16;
    if (spawn_idx >= 0 && spawn_idx < n) {
      seed->stage_yoshi_shyguy_spawn_rng_seed_u32 =
          rng_owner[spawn_idx] != 0u ? frame_rng_data[spawn_idx + 1] : frame_rng_data[spawn_idx];
      seed->stage_yoshi_shyguy_spawn_rng_seed_valid_u8 = 1u;
    }
  }

  free(key_states);
  free(phase_states);
  free(spawn_states);
  free(group_states);
  free(prev_items);
  free(shyguy_slots);
  free(new_spawns);
  free(rng_owner);
  Py_RETURN_NONE;

fail:
  free(key_states);
  free(phase_states);
  free(spawn_states);
  free(group_states);
  free(prev_items);
  free(shyguy_slots);
  free(new_spawns);
  free(rng_owner);
  return NULL;
}

PyObject* msl_validation_derive_dream_whispy_wind_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* ref_obj = NULL;
  int num_players = 0;
  int stage_id = 0;
  double wind_speed_d = 0.0;
  double epsilon_d = 0.025;
  if (!PyArg_ParseTuple(args, "OOOOiidd", &seed_obj, &prev_input_obj, &input_obj, &ref_obj,
                        &num_players, &stage_id, &wind_speed_d, &epsilon_d)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* prev_input_arr =
      require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input_u8");
  PyArrayObject* input_arr = require_contiguous_array(input_obj, NPY_UINT8, 2, "input_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  if (seed_arr == NULL || prev_input_arr == NULL || input_arr == NULL || ref_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (n <= 0 || PyArray_DIM(prev_input_arr, 0) != n || PyArray_DIM(input_arr, 0) != n ||
      PyArray_DIM(ref_arr, 0) != n ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_u8_rows(prev_input_arr, n, (npy_intp)sizeof(MslInput), "prev_input_u8") != 0 ||
      require_u8_rows(input_arr, n, (npy_intp)sizeof(MslInput), "input_u8") != 0 ||
      require_u8_rows(ref_arr, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "seed/input/ref byte arrays must have matching rows");
    }
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }

  npy_intp dims1[1] = {n};
  PyArrayObject* dir_arr = (PyArrayObject*)PyArray_SimpleNew(1, dims1, NPY_UINT8);
  PyArrayObject* valid_arr = (PyArrayObject*)PyArray_SimpleNew(1, dims1, NPY_UINT8);
  PyArrayObject* timer_arr = (PyArrayObject*)PyArray_SimpleNew(1, dims1, NPY_UINT16);
  if (dir_arr == NULL || valid_arr == NULL || timer_arr == NULL) {
    Py_XDECREF(dir_arr);
    Py_XDECREF(valid_arr);
    Py_XDECREF(timer_arr);
    return NULL;
  }
  uint8_t* dir = (uint8_t*)PyArray_DATA(dir_arr);
  uint8_t* valid = (uint8_t*)PyArray_DATA(valid_arr);
  uint16_t* timer = (uint16_t*)PyArray_DATA(timer_arr);
  memset(dir, 0, (size_t)n);
  memset(valid, 0, (size_t)n);
  memset(timer, 0, (size_t)n * sizeof(uint16_t));

  const uint8_t* seed_data = (const uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* prev_input_data = (const uint8_t*)PyArray_DATA(prev_input_arr);
  const uint8_t* input_data = (const uint8_t*)PyArray_DATA(input_arr);
  const uint8_t* ref_data = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t prev_input_stride = (size_t)PyArray_STRIDE(prev_input_arr, 0);
  const size_t input_stride = (size_t)PyArray_STRIDE(input_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);

  const npy_intp sim_chunk = 204;
  const int batch_size = (n < sim_chunk) ? (int)n : (int)sim_chunk;
  MslBatch* batch = msl_batch_create(batch_size, num_players);
  if (batch == NULL) {
    Py_DECREF(dir_arr);
    Py_DECREF(valid_arr);
    Py_DECREF(timer_arr);
    PyErr_SetString(PyExc_MemoryError, "msl_batch_create failed");
    return NULL;
  }
  (void)msl_batch_set_ucf_enabled(batch, 1);
  (void)msl_batch_set_ucf_cardinals_1_0_enabled(batch, 1);

  uint8_t* seed_scratch = NULL;
  uint8_t* prev_input_scratch = NULL;
  uint8_t* input_scratch = NULL;
  uint8_t* out_scratch = (uint8_t*)malloc((size_t)batch_size * sizeof(MslCompare));
  if (out_scratch == NULL) {
    msl_batch_destroy(batch);
    Py_DECREF(dir_arr);
    Py_DECREF(valid_arr);
    Py_DECREF(timer_arr);
    PyErr_NoMemory();
    return NULL;
  }
  if (batch_size > 1) {
    seed_scratch = (uint8_t*)malloc((size_t)batch_size * sizeof(MslSeed));
    prev_input_scratch = (uint8_t*)malloc((size_t)batch_size * sizeof(MslInput));
    input_scratch = (uint8_t*)malloc((size_t)batch_size * sizeof(MslInput));
    if (seed_scratch == NULL || prev_input_scratch == NULL || input_scratch == NULL) {
      free(seed_scratch);
      free(prev_input_scratch);
      free(input_scratch);
      free(out_scratch);
      msl_batch_destroy(batch);
      Py_DECREF(dir_arr);
      Py_DECREF(valid_arr);
      Py_DECREF(timer_arr);
      PyErr_NoMemory();
      return NULL;
    }
  }

  int err = 0;
  const float wind_speed = (float)wind_speed_d;
  const float eps = (float)epsilon_d;
  for (npy_intp start = 0; start < n; start += sim_chunk) {
    const npy_intp remaining = n - start;
    const int chunk_n = (int)((remaining < sim_chunk) ? remaining : sim_chunk);
    const uint8_t* chunk_seed = seed_data + (size_t)start * seed_stride;
    const uint8_t* chunk_prev_input = prev_input_data + (size_t)start * prev_input_stride;
    const uint8_t* chunk_input = input_data + (size_t)start * input_stride;
    uint8_t* chunk_out = out_scratch;
    size_t chunk_seed_stride = seed_stride;
    size_t chunk_prev_input_stride = prev_input_stride;
    size_t chunk_input_stride = input_stride;
    size_t chunk_out_stride = sizeof(MslCompare);
    if (chunk_n < batch_size) {
      for (int lane = 0; lane < chunk_n; lane++) {
        memcpy(seed_scratch + (size_t)lane * sizeof(MslSeed),
               chunk_seed + (size_t)lane * seed_stride, sizeof(MslSeed));
        memcpy(prev_input_scratch + (size_t)lane * sizeof(MslInput),
               chunk_prev_input + (size_t)lane * prev_input_stride, sizeof(MslInput));
        memcpy(input_scratch + (size_t)lane * sizeof(MslInput),
               chunk_input + (size_t)lane * input_stride, sizeof(MslInput));
      }
      for (int lane = chunk_n; lane < batch_size; lane++) {
        memcpy(seed_scratch + (size_t)lane * sizeof(MslSeed), seed_scratch, sizeof(MslSeed));
        memcpy(prev_input_scratch + (size_t)lane * sizeof(MslInput), prev_input_scratch,
               sizeof(MslInput));
        memcpy(input_scratch + (size_t)lane * sizeof(MslInput), input_scratch, sizeof(MslInput));
      }
      chunk_seed = seed_scratch;
      chunk_prev_input = prev_input_scratch;
      chunk_input = input_scratch;
      chunk_out = out_scratch;
      chunk_seed_stride = sizeof(MslSeed);
      chunk_prev_input_stride = sizeof(MslInput);
      chunk_input_stride = sizeof(MslInput);
      chunk_out_stride = sizeof(MslCompare);
    }
    err = msl_batch_reseed_seed(batch, chunk_seed, chunk_seed_stride);
    if (err == 0) {
      err = msl_batch_step_input(batch, chunk_prev_input, chunk_prev_input_stride, chunk_input,
                                 chunk_input_stride);
    }
    if (err == 0) {
      err = msl_batch_write_compare(batch, chunk_out, chunk_out_stride);
    }
    if (err != 0) {
      break;
    }
    for (int lane = 0; lane < chunk_n; lane++) {
      const npy_intp i = start + (npy_intp)lane;
      if (i + 1 >= n) {
        continue;
      }
      const MslSeed* seed = (const MslSeed*)(const void*)(seed_data + (size_t)i * seed_stride);
      const MslSeed* next_seed =
          (const MslSeed*)(const void*)(seed_data + (size_t)(i + 1) * seed_stride);
      if (seed->stage_id != (uint32_t)stage_id || next_seed->stage_id != (uint32_t)stage_id ||
          next_seed->frame_id != seed->frame_id + 1) {
        continue;
      }
      const MslCompare* ref = (const MslCompare*)(const void*)(ref_data + (size_t)i * ref_stride);
      const MslCompare* out =
          (const MslCompare*)(const void*)(chunk_out + (size_t)lane * chunk_out_stride);
      uint8_t row_dir = 0u;
      uint8_t conflict = 0u;
      for (int p = 0; p < num_players; p++) {
        const float diff = ref->pos_x[p] - out->pos_x[p];
        if (fabsf(fabsf(diff) - wind_speed) > eps) {
          continue;
        }
        const uint8_t d = (diff < 0.0f) ? 1u : 2u;
        if (row_dir != 0u && row_dir != d) {
          conflict = 1u;
          break;
        }
        row_dir = d;
      }
      if (row_dir != 0u && conflict == 0u) {
        // Replay seeds Whispy's hidden `grOldPupupu.xDC` one row after the source wind
        // displacement becomes visible in fighter position.
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        // refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4}
        dir[i + 1] = row_dir;
        valid[i + 1] = 1u;
      }
    }
  }
  free(seed_scratch);
  free(prev_input_scratch);
  free(input_scratch);
  free(out_scratch);
  msl_batch_destroy(batch);
  if (err != 0) {
    Py_DECREF(dir_arr);
    Py_DECREF(valid_arr);
    Py_DECREF(timer_arr);
    PyErr_Format(PyExc_RuntimeError, "Dream Whispy derivation sim failed: %d", err);
    return NULL;
  }

  npy_intp prev_sparse_i = -1;
  uint8_t prev_sparse_dir = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint8_t d = dir[i];
    if (valid[i] == 0u || (d != 1u && d != 2u)) {
      continue;
    }
    if (prev_sparse_i >= 0 && d == prev_sparse_dir && i - prev_sparse_i <= 274) {
      uint8_t contiguous = 1u;
      for (npy_intp j = prev_sparse_i; j < i; j++) {
        const MslSeed* a = (const MslSeed*)(const void*)(seed_data + (size_t)j * seed_stride);
        const MslSeed* b = (const MslSeed*)(const void*)(seed_data + (size_t)(j + 1) * seed_stride);
        if (a->stage_id != (uint32_t)stage_id || b->stage_id != (uint32_t)stage_id ||
            b->frame_id != a->frame_id + (int32_t)1) {
          contiguous = 0u;
          break;
        }
      }
      if (contiguous != 0u) {
        for (npy_intp j = prev_sparse_i + 1; j < i; j++) {
          dir[j] = d;
          valid[j] = 1u;
        }
      }
    }
    prev_sparse_i = i;
    prev_sparse_dir = d;
  }

  uint8_t episode_dir = 0u;
  uint16_t episode_age = 0u;
  for (npy_intp i = 0; i < n; i++) {
    if (valid[i] == 0u || dir[i] == 0u) {
      episode_dir = 0u;
      episode_age = 0u;
      continue;
    }
    if (dir[i] != episode_dir) {
      episode_dir = dir[i];
      episode_age = 0u;
    }
    timer[i] = (episode_age < 274u) ? (uint16_t)(274u - episode_age) : 1u;
    if (episode_age < 274u) {
      episode_age++;
    }
  }

  return Py_BuildValue("(NNN)", dir_arr, valid_arr, timer_arr);
}
