/* Native validation stage-lane materialization. */

#include "msl_validation_stage.h"
#include "msl_preprocess.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/action_ids.h"
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

/* Preprocess entrypoints owned by this validation family. */

PyObject* msl_derive_yoshi_shyguy_seed_lanes_py(PyObject* self, PyObject* args) {
  PyObject* exists_obj = NULL;
  PyObject* type_obj = NULL;
  PyObject* owner_obj = NULL;
  PyObject* state_obj = NULL;
  PyObject* spawn_obj = NULL;
  PyObject* iid_obj = NULL;
  PyObject* vel_x_obj = NULL;
  PyObject* vel_y_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* damage_obj = NULL;
  PyObject* vpos_obj = NULL;
  PyObject* speeds_obj = NULL;
  PyObject* dyn_obj = NULL;
  int replay_stage_id = 0;
  int param_stage_id = 0;
  int item_kind = 0;
  int timer_reset = 0;
  int spawn_delay_step = 0;
  double state4_speed_mul = 0.0;
  double hitlag_damage_mul = 0.0;
  double hitlag_base = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOiiiiidOOOdd", &exists_obj, &type_obj, &owner_obj,
                        &state_obj, &spawn_obj, &iid_obj, &vel_x_obj, &vel_y_obj, &pos_x_obj,
                        &pos_y_obj, &damage_obj, &replay_stage_id, &param_stage_id, &item_kind,
                        &timer_reset, &spawn_delay_step, &state4_speed_mul, &vpos_obj, &speeds_obj,
                        &dyn_obj, &hitlag_damage_mul, &hitlag_base)) {
    return NULL;
  }

  PyArrayObject* exists = require_contiguous_array(exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* type = require_contiguous_array(type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* owner = require_contiguous_array(owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* state = require_contiguous_array(state_obj, NPY_UINT8, 2, "item_state_u8");
  PyArrayObject* spawn = require_contiguous_array(spawn_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* iid = require_contiguous_array(iid_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* vel_x = require_contiguous_array(vel_x_obj, NPY_FLOAT32, 2, "item_vel_x_f32");
  PyArrayObject* vel_y = require_contiguous_array(vel_y_obj, NPY_FLOAT32, 2, "item_vel_y_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "item_pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "item_pos_y_f32");
  PyArrayObject* damage = require_contiguous_array(damage_obj, NPY_UINT16, 2, "item_damage_u16");
  PyArrayObject* vpos_arr = require_contiguous_array(vpos_obj, NPY_FLOAT32, 1, "shyguy_vpos_f32");
  PyArrayObject* speeds_arr =
      require_contiguous_array(speeds_obj, NPY_FLOAT32, 1, "shyguy_speeds_f32");
  PyArrayObject* dyn_arr = require_contiguous_array(dyn_obj, NPY_FLOAT32, 1, "shyguy_dyn_y_f32");
  if (exists == NULL || type == NULL || owner == NULL || state == NULL || spawn == NULL ||
      iid == NULL || vel_x == NULL || vel_y == NULL || pos_x == NULL || pos_y == NULL ||
      damage == NULL || vpos_arr == NULL || speeds_arr == NULL || dyn_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(exists, 0);
  const npy_intp slots = PyArray_DIM(exists, 1);
  if (slots < 0 || slots > MSL_MAX_ITEMS) {
    PyErr_SetString(PyExc_ValueError, "Yoshi Shy Guy item width exceeds MSL_MAX_ITEMS");
    return NULL;
  }
  if (require_exact_2d_shape(type, n, slots, "item_type_u16") != 0 ||
      require_exact_2d_shape(owner, n, slots, "item_owner_i8") != 0 ||
      require_exact_2d_shape(state, n, slots, "item_state_u8") != 0 ||
      require_exact_2d_shape(spawn, n, slots, "item_spawn_id_u32") != 0 ||
      require_exact_2d_shape(iid, n, slots, "item_instance_id_u16") != 0 ||
      require_exact_2d_shape(vel_x, n, slots, "item_vel_x_f32") != 0 ||
      require_exact_2d_shape(vel_y, n, slots, "item_vel_y_f32") != 0 ||
      require_exact_2d_shape(pos_x, n, slots, "item_pos_x_f32") != 0 ||
      require_exact_2d_shape(pos_y, n, slots, "item_pos_y_f32") != 0 ||
      require_exact_2d_shape(damage, n, slots, "item_damage_u16") != 0) {
    return NULL;
  }
  if (PyArray_SIZE(vpos_arr) < MSL_YOSHI_SHYGUY_VPOS_COUNT ||
      PyArray_SIZE(speeds_arr) < MSL_YOSHI_SHYGUY_SPEED_COUNT ||
      PyArray_SIZE(dyn_arr) < MSL_YOSHI_SHYGUY_DYN_Y_COUNT) {
    PyErr_SetString(PyExc_ValueError, "Yoshi Shy Guy param arrays have invalid lengths");
    return NULL;
  }

  const size_t item_bytes = sizeof(((MslSeed*)0)->items);
  npy_intp seed_dims[2] = {n, (npy_intp)sizeof(MslSeed)};
  npy_intp item_dims[2] = {n, (npy_intp)item_bytes};
  npy_intp rng_dims[1] = {n + 1};
  PyArrayObject* seed_arr = (PyArrayObject*)PyArray_ZEROS(2, seed_dims, NPY_UINT8, 0);
  PyArrayObject* items_arr = (PyArrayObject*)PyArray_ZEROS(2, item_dims, NPY_UINT8, 0);
  PyArrayObject* frame_rng_arr = (PyArrayObject*)PyArray_ZEROS(1, rng_dims, NPY_UINT32, 0);
  if (seed_arr == NULL || items_arr == NULL || frame_rng_arr == NULL) {
    Py_XDECREF(seed_arr);
    Py_XDECREF(items_arr);
    Py_XDECREF(frame_rng_arr);
    return NULL;
  }

  uint8_t* items_bytes = (uint8_t*)PyArray_DATA(items_arr);
  const size_t items_stride = (size_t)PyArray_STRIDE(items_arr, 0);
  const uint8_t* ex = (const uint8_t*)PyArray_DATA(exists);
  const uint16_t* ty = (const uint16_t*)PyArray_DATA(type);
  const int8_t* ow = (const int8_t*)PyArray_DATA(owner);
  const uint8_t* st = (const uint8_t*)PyArray_DATA(state);
  const uint32_t* sp = (const uint32_t*)PyArray_DATA(spawn);
  const uint16_t* iidp = (const uint16_t*)PyArray_DATA(iid);
  const float* vx = (const float*)PyArray_DATA(vel_x);
  const float* vy = (const float*)PyArray_DATA(vel_y);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const uint16_t* dmg = (const uint16_t*)PyArray_DATA(damage);
  for (npy_intp fi = 0; fi < n; fi++) {
    MslItem* row = (MslItem*)(void*)(items_bytes + (size_t)fi * items_stride);
    for (npy_intp slot = 0; slot < slots; slot++) {
      const npy_intp idx = fi * slots + slot;
      MslItem* item = &row[slot];
      item->exists = ex[idx];
      item->type = ty[idx];
      item->owner = ow[idx];
      item->state = st[idx];
      item->spawn_id = sp[idx];
      item->instance_id = iidp[idx];
      item->vel_x = vx[idx];
      item->vel_y = vy[idx];
      item->pos_x = px[idx];
      item->pos_y = py[idx];
      item->damage = dmg[idx];
    }
  }

  PyObject* core_args =
      Py_BuildValue("OOOOOOiiiiiddd", seed_arr, items_arr, frame_rng_arr, vpos_arr, speeds_arr,
                    dyn_arr, replay_stage_id, param_stage_id, item_kind, timer_reset,
                    spawn_delay_step, state4_speed_mul, hitlag_damage_mul, hitlag_base);
  if (core_args == NULL) {
    Py_DECREF(seed_arr);
    Py_DECREF(items_arr);
    Py_DECREF(frame_rng_arr);
    return NULL;
  }
  PyObject* core_result = msl_validation_derive_yoshi_shyguy_buffers_py(self, core_args);
  Py_DECREF(core_args);
  if (core_result == NULL) {
    Py_DECREF(seed_arr);
    Py_DECREF(items_arr);
    Py_DECREF(frame_rng_arr);
    return NULL;
  }
  Py_DECREF(core_result);

  npy_intp dims1[1] = {n};
  npy_intp dims2[2] = {n, slots};
  PyArrayObject* prev_vel_y = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_FLOAT32, 0);
  PyArrayObject* prev_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* phase = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* phase_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* timer = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT16, 0);
  PyArrayObject* pattern = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT8, 0);
  PyArrayObject* stage_valid = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT8, 0);
  PyArrayObject* speed_index = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* speed_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* delay = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* delay_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* hitlag = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* hitlag_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  if (prev_vel_y == NULL || prev_valid == NULL || phase == NULL || phase_valid == NULL ||
      timer == NULL || pattern == NULL || stage_valid == NULL || speed_index == NULL ||
      speed_valid == NULL || delay == NULL || delay_valid == NULL || hitlag == NULL ||
      hitlag_valid == NULL) {
    Py_XDECREF(prev_vel_y);
    Py_XDECREF(prev_valid);
    Py_XDECREF(phase);
    Py_XDECREF(phase_valid);
    Py_XDECREF(timer);
    Py_XDECREF(pattern);
    Py_XDECREF(stage_valid);
    Py_XDECREF(speed_index);
    Py_XDECREF(speed_valid);
    Py_XDECREF(delay);
    Py_XDECREF(delay_valid);
    Py_XDECREF(hitlag);
    Py_XDECREF(hitlag_valid);
    Py_DECREF(seed_arr);
    Py_DECREF(items_arr);
    Py_DECREF(frame_rng_arr);
    return NULL;
  }

  const uint8_t* seed_data = (const uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  float* prev_vel_y_p = (float*)PyArray_DATA(prev_vel_y);
  uint8_t* prev_valid_p = (uint8_t*)PyArray_DATA(prev_valid);
  uint8_t* phase_p = (uint8_t*)PyArray_DATA(phase);
  uint8_t* phase_valid_p = (uint8_t*)PyArray_DATA(phase_valid);
  uint16_t* timer_p = (uint16_t*)PyArray_DATA(timer);
  uint8_t* pattern_p = (uint8_t*)PyArray_DATA(pattern);
  uint8_t* stage_valid_p = (uint8_t*)PyArray_DATA(stage_valid);
  uint8_t* speed_index_p = (uint8_t*)PyArray_DATA(speed_index);
  uint8_t* speed_valid_p = (uint8_t*)PyArray_DATA(speed_valid);
  uint16_t* delay_p = (uint16_t*)PyArray_DATA(delay);
  uint8_t* delay_valid_p = (uint8_t*)PyArray_DATA(delay_valid);
  uint8_t* hitlag_p = (uint8_t*)PyArray_DATA(hitlag);
  uint8_t* hitlag_valid_p = (uint8_t*)PyArray_DATA(hitlag_valid);
  for (npy_intp fi = 0; fi < n; fi++) {
    const MslSeed* seed = (const MslSeed*)(const void*)(seed_data + (size_t)fi * seed_stride);
    timer_p[fi] = seed->stage_yoshi_shyguy_timer_u16;
    pattern_p[fi] = seed->stage_yoshi_shyguy_pattern_u8;
    stage_valid_p[fi] = seed->stage_yoshi_shyguy_valid_u8;
    for (npy_intp slot = 0; slot < slots; slot++) {
      const npy_intp idx = fi * slots + slot;
      prev_vel_y_p[idx] = seed->item_shyguy_prev_vel_y[slot];
      prev_valid_p[idx] = seed->item_shyguy_prev_vel_y_valid[slot];
      phase_p[idx] = seed->item_shyguy_dyn_y_phase_u8[slot];
      phase_valid_p[idx] = seed->item_shyguy_dyn_y_phase_valid_u8[slot];
      speed_index_p[idx] = seed->item_shyguy_speed_index_u8[slot];
      speed_valid_p[idx] = seed->item_shyguy_speed_index_valid_u8[slot];
      delay_p[idx] = seed->item_shyguy_delay_u16[slot];
      delay_valid_p[idx] = seed->item_shyguy_delay_valid_u8[slot];
      hitlag_p[idx] = seed->item_shyguy_hitlag_u8[slot];
      hitlag_valid_p[idx] = seed->item_shyguy_hitlag_valid_u8[slot];
    }
  }

  Py_DECREF(seed_arr);
  Py_DECREF(items_arr);
  Py_DECREF(frame_rng_arr);
  return Py_BuildValue("NNNNNNNNNNNNN", prev_vel_y, prev_valid, phase, phase_valid, timer, pattern,
                       stage_valid, speed_index, speed_valid, delay, delay_valid, hitlag,
                       hitlag_valid);
}

PyObject* msl_derive_dream_whispy_wind_seed_lanes_py(PyObject* self, PyObject* args) {
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
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_bytes");
  PyArrayObject* prev_input_arr =
      require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input_bytes");
  PyArrayObject* input_arr = require_contiguous_array(input_obj, NPY_UINT8, 2, "input_bytes");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_bytes");
  if (seed_arr == NULL || prev_input_arr == NULL || input_arr == NULL || ref_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (n <= 0 || PyArray_NDIM(seed_arr) != 2 || PyArray_NDIM(prev_input_arr) != 2 ||
      PyArray_NDIM(input_arr) != 2 || PyArray_NDIM(ref_arr) != 2 ||
      PyArray_DIM(prev_input_arr, 0) != n || PyArray_DIM(input_arr, 0) != n ||
      PyArray_DIM(ref_arr, 0) != n) {
    PyErr_SetString(PyExc_ValueError, "seed/input/ref byte arrays must be 2D with matching rows");
    return NULL;
  }
  if (PyArray_DIM(seed_arr, 1) < (npy_intp)sizeof(MslSeed) ||
      PyArray_DIM(prev_input_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input_arr, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(ref_arr, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "seed/input/ref byte array has a short row stride");
    return NULL;
  }
  if (num_players <= 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  return msl_validation_derive_dream_whispy_wind_seed_lanes_py(self, args);
}

typedef struct MslPyFodLineTransform {
  uint16_t line_id;
  uint8_t platform_id;
  double height_coeff;
  double local_y;
  double x0;
  double x1;
} MslPyFodLineTransform;

typedef struct MslPyFodHardFloor {
  double x0;
  double y0;
  double x1;
  double y1;
} MslPyFodHardFloor;

// Native mirrors of the replay-preprocessing FoD floor-skip thresholds:
// transformed-platform lookup slop, one ECB vertical unit, and floor crossing Y bias.
static const double FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP = 2.0;
static const double FOD_SKIP_ECB_VERTICAL_UNIT = 1.0;
static const double FOD_FLOOR_Y_BIAS = 0.0001;
static const double FOD_SKIP_SHALLOW_ECB_UNITS = 2.0;

static const MslPyFodLineTransform* msl_py_find_fod_line_transform(
    const MslPyFodLineTransform* transforms, npy_intp count, uint16_t line_id) {
  for (npy_intp i = 0; i < count; i++) {
    if (transforms[i].line_id == line_id) {
      return &transforms[i];
    }
  }
  return NULL;
}

static inline float msl_py_fod_absf(float x) { return x < 0.0f ? -x : x; }

static inline void msl_py_fod_advance_height(float h, float vel, int use_motion_params, double home,
                                             double max_h, double min_visible, double hidden,
                                             float* out_h, uint8_t* out_keep_velocity) {
  if (!use_motion_params) {
    *out_h = (float)((double)h + (double)vel);
    *out_keep_velocity = 1u;
    return;
  }
  double target = max_h;
  if (vel < 0.0f) {
    target = (double)h <= min_visible + fabs((double)vel) * 4.0 ? hidden : min_visible;
  } else if (vel > 0.0f) {
    target = (double)h < home ? home : max_h;
  }
  const float next = (float)((double)h + (double)vel);
  if (vel > 0.0f && next >= target) {
    *out_h = (float)target;
    *out_keep_velocity = 0u;
    return;
  }
  if (vel < 0.0f && next <= target) {
    *out_h = (float)target;
    *out_keep_velocity = 0u;
    return;
  }
  *out_h = next;
  *out_keep_velocity = 1u;
}

PyObject* msl_derive_fod_platform_motion_with_ground_contact_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* heights_obj = NULL;
  PyObject* valid_obj = NULL;
  PyObject* event_fresh_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* ground_id_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* next_on_ground_obj = NULL;
  PyObject* next_ground_id_obj = NULL;
  PyObject* next_pos_y_obj = NULL;
  PyObject* line_id_obj = NULL;
  PyObject* platform_id_obj = NULL;
  PyObject* height_coeff_obj = NULL;
  PyObject* local_y_obj = NULL;
  int use_motion_params = 0;
  double home_d = 0.0;
  double max_h_d = 0.0;
  double min_visible_d = 0.0;
  double hidden_d = 0.0;
  int return_source = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOiddddi", &heights_obj, &valid_obj, &event_fresh_obj,
                        &on_ground_obj, &ground_id_obj, &pos_y_obj, &next_on_ground_obj,
                        &next_ground_id_obj, &next_pos_y_obj, &line_id_obj, &platform_id_obj,
                        &height_coeff_obj, &local_y_obj, &use_motion_params, &home_d, &max_h_d,
                        &min_visible_d, &hidden_d, &return_source)) {
    return NULL;
  }

  PyArrayObject* heights =
      require_contiguous_array_readonly(heights_obj, NPY_FLOAT32, 2, "fod_heights_f32");
  PyArrayObject* valid = require_contiguous_array_readonly(valid_obj, NPY_UINT8, 2, "fod_valid_u8");
  PyArrayObject* event_fresh = NULL;
  if (event_fresh_obj != Py_None) {
    event_fresh =
        require_contiguous_array_readonly(event_fresh_obj, NPY_UINT8, 2, "fod_event_fresh_u8");
  }
  PyArrayObject* on_ground =
      require_contiguous_array_readonly(on_ground_obj, NPY_UINT8, 2, "post_on_ground_u8");
  PyArrayObject* ground_id =
      require_contiguous_array_readonly(ground_id_obj, NPY_UINT16, 2, "post_ground_id_u16");
  PyArrayObject* pos_y =
      require_contiguous_array_readonly(pos_y_obj, NPY_FLOAT32, 2, "post_pos_y_f32");
  PyArrayObject* next_on_ground = NULL;
  PyArrayObject* next_ground_id = NULL;
  PyArrayObject* next_pos_y = NULL;
  const int has_next = next_on_ground_obj != Py_None;
  if ((next_on_ground_obj == Py_None) != (next_ground_id_obj == Py_None) ||
      (next_on_ground_obj == Py_None) != (next_pos_y_obj == Py_None)) {
    PyErr_SetString(PyExc_ValueError,
                    "FoD next-post grounded-contact arrays must be supplied together");
    return NULL;
  }
  if (has_next) {
    next_on_ground =
        require_contiguous_array_readonly(next_on_ground_obj, NPY_UINT8, 2, "next_on_ground_u8");
    next_ground_id =
        require_contiguous_array_readonly(next_ground_id_obj, NPY_UINT16, 2, "next_ground_id_u16");
    next_pos_y =
        require_contiguous_array_readonly(next_pos_y_obj, NPY_FLOAT32, 2, "next_pos_y_f32");
  }
  PyArrayObject* line_id =
      require_contiguous_array_readonly(line_id_obj, NPY_UINT16, 1, "fod_line_ids_u16");
  PyArrayObject* platform_id =
      require_contiguous_array_readonly(platform_id_obj, NPY_UINT8, 1, "fod_platform_ids_u8");
  PyArrayObject* height_coeff =
      require_contiguous_array_readonly(height_coeff_obj, NPY_FLOAT64, 1, "fod_height_coeff_f64");
  PyArrayObject* local_y =
      require_contiguous_array_readonly(local_y_obj, NPY_FLOAT64, 1, "fod_local_y_f64");

  if (heights == NULL || valid == NULL || (event_fresh_obj != Py_None && event_fresh == NULL) ||
      on_ground == NULL || ground_id == NULL || pos_y == NULL ||
      (has_next && (next_on_ground == NULL || next_ground_id == NULL || next_pos_y == NULL)) ||
      line_id == NULL || platform_id == NULL || height_coeff == NULL || local_y == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(heights, 0);
  const npy_intp players = PyArray_DIM(on_ground, 1);
  const npy_intp n_transforms = PyArray_DIM(line_id, 0);
  if (PyArray_NDIM(heights) != 2 || PyArray_DIM(heights, 1) != 2 ||
      require_exact_2d_shape(valid, n, 2, "fod_valid_u8") < 0 ||
      (event_fresh != NULL &&
       require_exact_2d_shape(event_fresh, n, 2, "fod_event_fresh_u8") < 0) ||
      require_exact_2d_shape(on_ground, n, players, "post_on_ground_u8") < 0 ||
      require_exact_2d_shape(ground_id, n, players, "post_ground_id_u16") < 0 ||
      require_exact_2d_shape(pos_y, n, players, "post_pos_y_f32") < 0 ||
      (has_next && require_exact_2d_shape(next_on_ground, n, players, "next_on_ground_u8") < 0) ||
      (has_next && require_exact_2d_shape(next_ground_id, n, players, "next_ground_id_u16") < 0) ||
      (has_next && require_exact_2d_shape(next_pos_y, n, players, "next_pos_y_f32") < 0) ||
      PyArray_NDIM(line_id) != 1 || PyArray_NDIM(platform_id) != 1 ||
      PyArray_NDIM(height_coeff) != 1 || PyArray_NDIM(local_y) != 1 ||
      PyArray_DIM(platform_id, 0) != n_transforms || PyArray_DIM(height_coeff, 0) != n_transforms ||
      PyArray_DIM(local_y, 0) != n_transforms) {
    PyErr_SetString(PyExc_ValueError, "FoD platform-motion inputs have incompatible shapes");
    return NULL;
  }

  npy_intp dims2[2] = {n, 2};
  PyArrayObject* out_h = (PyArrayObject*)PyArray_SimpleNew(2, dims2, NPY_FLOAT32);
  PyArrayObject* out_v = (PyArrayObject*)PyArray_SimpleNew(2, dims2, NPY_UINT8);
  PyArrayObject* out_vel = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_FLOAT32, 0);
  PyArrayObject* out_vel_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* out_source = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  if (out_h == NULL || out_v == NULL || out_vel == NULL || out_vel_valid == NULL ||
      out_source == NULL) {
    Py_XDECREF(out_h);
    Py_XDECREF(out_v);
    Py_XDECREF(out_vel);
    Py_XDECREF(out_vel_valid);
    Py_XDECREF(out_source);
    return NULL;
  }
  memcpy(PyArray_DATA(out_h), PyArray_DATA(heights), (size_t)(n * 2) * sizeof(float));
  memcpy(PyArray_DATA(out_v), PyArray_DATA(valid), (size_t)(n * 2) * sizeof(uint8_t));

  MslPyFodLineTransform transforms[64];
  if (n_transforms > (npy_intp)(sizeof(transforms) / sizeof(transforms[0]))) {
    PyErr_SetString(PyExc_ValueError, "too many FoD line transforms");
    Py_DECREF(out_h);
    Py_DECREF(out_v);
    Py_DECREF(out_vel);
    Py_DECREF(out_vel_valid);
    Py_DECREF(out_source);
    return NULL;
  }
  const uint16_t* line_idp = (const uint16_t*)PyArray_DATA(line_id);
  const uint8_t* platform_idp = (const uint8_t*)PyArray_DATA(platform_id);
  const double* coeffp = (const double*)PyArray_DATA(height_coeff);
  const double* local_yp = (const double*)PyArray_DATA(local_y);
  for (npy_intp i = 0; i < n_transforms; i++) {
    if (platform_idp[i] >= 2u) {
      PyErr_SetString(PyExc_ValueError, "FoD platform id must be 0 or 1");
      Py_DECREF(out_h);
      Py_DECREF(out_v);
      Py_DECREF(out_vel);
      Py_DECREF(out_vel_valid);
      Py_DECREF(out_source);
      return NULL;
    }
    transforms[i].line_id = line_idp[i];
    transforms[i].platform_id = platform_idp[i];
    transforms[i].height_coeff = coeffp[i];
    transforms[i].local_y = local_yp[i];
    transforms[i].x0 = 0.0;
    transforms[i].x1 = 0.0;
  }

  float* out_hp = (float*)PyArray_DATA(out_h);
  uint8_t* out_vp = (uint8_t*)PyArray_DATA(out_v);
  float* out_velp = (float*)PyArray_DATA(out_vel);
  uint8_t* out_vel_validp = (uint8_t*)PyArray_DATA(out_vel_valid);
  uint8_t* out_sourcep = (uint8_t*)PyArray_DATA(out_source);
  const uint8_t* event_freshp =
      event_fresh != NULL ? (const uint8_t*)PyArray_DATA(event_fresh) : NULL;
  const uint8_t* on_groundp = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* ground_idp = (const uint16_t*)PyArray_DATA(ground_id);
  const float* pos_yp = (const float*)PyArray_DATA(pos_y);
  const uint8_t* next_on_groundp = has_next ? (const uint8_t*)PyArray_DATA(next_on_ground) : NULL;
  const uint16_t* next_ground_idp = has_next ? (const uint16_t*)PyArray_DATA(next_ground_id) : NULL;
  const float* next_pos_yp = has_next ? (const float*)PyArray_DATA(next_pos_y) : NULL;

  float cur[2] = {n > 0 ? out_hp[0] : 0.0f, n > 0 ? out_hp[1] : 0.0f};
  uint8_t cur_valid[2] = {0u, 0u};
  float cur_vel[2] = {0.0f, 0.0f};
  uint8_t cur_vel_valid[2] = {0u, 0u};
  float last_obs_h[2] = {0.0f, 0.0f};
  int32_t last_obs_frame[2] = {-1, -1};
  uint8_t has_obs[2] = {0u, 0u};
  uint8_t contact_owned[2] = {0u, 0u};
  const double home = home_d;
  const double max_h = max_h_d;
  const double min_visible = min_visible_d;
  const double hidden = hidden_d;
  enum { SOURCE_EVENT = 0x01, SOURCE_CONTACT = 0x02, SOURCE_NEXT_CONTACT = 0x04 };
  const float eps = 1.0e-6f;
  const float fod_floor_y_bias = 0.0001f;

  for (npy_intp fi = 0; fi < n; fi++) {
    uint8_t current_contact_this_frame[2] = {0u, 0u};
    uint8_t source_this_frame[2] = {0u, 0u};
    float direct_event_height_this_frame[2] = {NAN, NAN};
    int32_t frame_start_obs_frame[2] = {last_obs_frame[0], last_obs_frame[1]};
    float frame_start_obs_h[2] = {last_obs_h[0], last_obs_h[1]};
    uint8_t frame_start_has_obs[2] = {has_obs[0], has_obs[1]};

    for (int platform = 0; platform < 2; platform++) {
      const npy_intp hp_idx = fi * 2 + platform;
      if (out_vp[hp_idx] == 0u) {
        continue;
      }
      const float event_h = out_hp[hp_idx];
      const bool fresh_event =
          event_freshp != NULL
              ? event_freshp[hp_idx] != 0u
              : !(cur_valid[platform] != 0u && msl_py_fod_absf(event_h - cur[platform]) <= eps);
      if (!fresh_event) {
        continue;
      }
      source_this_frame[platform] |= SOURCE_EVENT;
      direct_event_height_this_frame[platform] = event_h;
      const bool same_height =
          cur_valid[platform] != 0u && msl_py_fod_absf(event_h - cur[platform]) <= eps;
      const bool predicted_motion =
          same_height && cur_vel_valid[platform] != 0u && cur_vel[platform] < -eps;
      if (predicted_motion) {
        /* keep existing velocity */
      } else if (same_height) {
        cur_vel[platform] = 0.0f;
        cur_vel_valid[platform] = 0u;
      } else if (has_obs[platform] != 0u && fi > (npy_intp)last_obs_frame[platform]) {
        const float delta = (float)(((double)event_h - (double)last_obs_h[platform]) /
                                    (double)(fi - last_obs_frame[platform]));
        if (isfinite(delta) && msl_py_fod_absf(delta) > eps) {
          cur_vel[platform] = delta;
          cur_vel_valid[platform] = 1u;
        } else {
          cur_vel[platform] = 0.0f;
          cur_vel_valid[platform] = 0u;
        }
      } else {
        cur_vel[platform] = 0.0f;
        cur_vel_valid[platform] = 0u;
      }
      cur[platform] = event_h;
      cur_valid[platform] = 1u;
      last_obs_h[platform] = event_h;
      last_obs_frame[platform] = (int32_t)fi;
      has_obs[platform] = 1u;
      contact_owned[platform] = 0u;
    }

    for (npy_intp slot = 0; slot < players; slot++) {
      const npy_intp idx = fi * players + slot;
      if (on_groundp[idx] == 0u) {
        continue;
      }
      const MslPyFodLineTransform* rec =
          msl_py_find_fod_line_transform(transforms, n_transforms, ground_idp[idx]);
      if (rec == NULL) {
        continue;
      }
      const int platform = (int)rec->platform_id;
      const float y = pos_yp[idx];
      if (!isfinite(y) || rec->height_coeff == 0.0) {
        continue;
      }
      const float h = (float)(((double)y - rec->local_y) / rec->height_coeff);
      bool derived_velocity = false;
      if (frame_start_has_obs[platform] != 0u && fi > (npy_intp)frame_start_obs_frame[platform]) {
        const float delta = (float)(((double)h - (double)frame_start_obs_h[platform]) /
                                    (double)(fi - frame_start_obs_frame[platform]));
        cur_vel[platform] = delta;
        cur_vel_valid[platform] = 1u;
        derived_velocity = true;
      } else {
        const float direct_event_h = direct_event_height_this_frame[platform];
        if (isfinite(direct_event_h) && msl_py_fod_absf(h - direct_event_h) > eps) {
          cur_vel[platform] = (float)((double)h - (double)direct_event_h);
          cur_vel_valid[platform] = 1u;
          derived_velocity = true;
        }
      }
      if (!derived_velocity && has_obs[platform] != 0u && fi > (npy_intp)last_obs_frame[platform]) {
        const float delta = (float)(((double)h - (double)last_obs_h[platform]) /
                                    (double)(fi - last_obs_frame[platform]));
        if (isfinite(delta)) {
          cur_vel[platform] = delta;
          cur_vel_valid[platform] = 1u;
          derived_velocity = true;
        }
      }
      if (!derived_velocity && contact_owned[platform] == 0u) {
        cur_vel[platform] = 0.0f;
        cur_vel_valid[platform] = 0u;
      }
      cur[platform] = h;
      cur_valid[platform] = 1u;
      last_obs_h[platform] = h;
      last_obs_frame[platform] = (int32_t)fi;
      has_obs[platform] = 1u;
      contact_owned[platform] = 1u;
      current_contact_this_frame[platform] = 1u;
      source_this_frame[platform] |= SOURCE_CONTACT;
    }

    if (has_next) {
      for (npy_intp slot = 0; slot < players; slot++) {
        const npy_intp idx = fi * players + slot;
        if (next_on_groundp[idx] == 0u) {
          continue;
        }
        const MslPyFodLineTransform* rec =
            msl_py_find_fod_line_transform(transforms, n_transforms, next_ground_idp[idx]);
        if (rec == NULL) {
          continue;
        }
        const int platform = (int)rec->platform_id;
        const float y = next_pos_yp[idx];
        if (!isfinite(y) || rec->height_coeff == 0.0) {
          continue;
        }
        if (current_contact_this_frame[platform] != 0u) {
          continue;
        }
        if (cur_vel_valid[platform] != 0u && msl_py_fod_absf(cur_vel[platform]) > eps) {
          continue;
        }
        const float h = (float)(((double)y - rec->local_y - (2.0 * (double)fod_floor_y_bias)) /
                                rec->height_coeff);
        if (cur_valid[platform] != 0u && msl_py_fod_absf(h - cur[platform]) <= eps) {
          continue;
        }
        cur[platform] = h;
        cur_valid[platform] = 1u;
        cur_vel[platform] = 0.0f;
        cur_vel_valid[platform] = 0u;
        last_obs_h[platform] = h;
        last_obs_frame[platform] = (int32_t)fi;
        has_obs[platform] = 1u;
        contact_owned[platform] = 1u;
        source_this_frame[platform] |= SOURCE_NEXT_CONTACT;
      }
    }

    for (int platform = 0; platform < 2; platform++) {
      const npy_intp hp_idx = fi * 2 + platform;
      if (cur_valid[platform] != 0u) {
        out_hp[hp_idx] = cur[platform];
        out_vp[hp_idx] = 1u;
      }
      if (cur_vel_valid[platform] != 0u) {
        out_velp[hp_idx] = cur_vel[platform];
        out_vel_validp[hp_idx] = 1u;
      }
      out_sourcep[hp_idx] = source_this_frame[platform];
    }

    for (int platform = 0; platform < 2; platform++) {
      if (cur_valid[platform] != 0u && cur_vel_valid[platform] != 0u) {
        float next_h = cur[platform];
        uint8_t keep_velocity = 0u;
        msl_py_fod_advance_height(cur[platform], cur_vel[platform], use_motion_params, home, max_h,
                                  min_visible, hidden, &next_h, &keep_velocity);
        cur[platform] = next_h;
        if (keep_velocity == 0u) {
          cur_vel_valid[platform] = 0u;
        }
      }
    }
  }

  if (return_source) {
    return Py_BuildValue("(NNNNN)", out_h, out_v, out_vel, out_vel_valid, out_source);
  }
  Py_DECREF(out_source);
  return Py_BuildValue("(NNNN)", out_h, out_v, out_vel, out_vel_valid);
}

static inline uint8_t msl_py_fod_lut_u8(const uint8_t* lut, npy_intp width, uint8_t cid,
                                        uint16_t action) {
  return ((npy_intp)action < width) ? lut[(npy_intp)cid * width + (npy_intp)action] : 0u;
}

static inline int16_t msl_py_fod_lut_i16(const int16_t* lut, npy_intp width, uint8_t cid,
                                         uint16_t action) {
  return ((npy_intp)action < width) ? lut[(npy_intp)cid * width + (npy_intp)action] : -1;
}

static bool msl_py_fod_hard_floor_root_crossing(const MslPyFodHardFloor* floors,
                                                npy_intp floor_count, double x, double y0,
                                                double y1) {
  const double dx_eps = 1.0e-6;
  const double x_end_clamp = 0.1;
  if (!(y1 < y0)) {
    return false;
  }
  for (npy_intp i = 0; i < floor_count; i++) {
    const MslPyFodHardFloor* seg = &floors[i];
    const double dx = seg->x1 - seg->x0;
    if (fabs(dx) <= dx_eps) {
      continue;
    }
    const double lo_x = fmin(seg->x0, seg->x1) - x_end_clamp;
    const double hi_x = fmax(seg->x0, seg->x1) + x_end_clamp;
    if (x < lo_x || x > hi_x) {
      continue;
    }
    const double t = (x - seg->x0) / dx;
    const double world_y = seg->y0 + ((seg->y1 - seg->y0) * t);
    if (y0 > world_y + FOD_FLOOR_Y_BIAS && y1 < world_y) {
      return true;
    }
  }
  return false;
}

PyObject* msl_derive_fod_floor_skip_segments_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* speed_y_self_obj = NULL;
  PyObject* speed_y_attack_obj = NULL;
  PyObject* prev_main_y_obj = NULL;
  PyObject* main_y_obj = NULL;
  PyObject* platform_h_obj = NULL;
  PyObject* platform_valid_obj = NULL;
  PyObject* line_id_obj = NULL;
  PyObject* platform_id_obj = NULL;
  PyObject* rec_x0_obj = NULL;
  PyObject* rec_x1_obj = NULL;
  PyObject* height_coeff_obj = NULL;
  PyObject* local_y_obj = NULL;
  PyObject* hard_x0_obj = NULL;
  PyObject* hard_y0_obj = NULL;
  PyObject* hard_x1_obj = NULL;
  PyObject* hard_y1_obj = NULL;
  PyObject* active_lut_obj = NULL;
  PyObject* attackair_lut_obj = NULL;
  PyObject* common_lut_obj = NULL;
  PyObject* shallow_lut_obj = NULL;
  PyObject* phase_start_obj = NULL;
  PyObject* phase_stop_obj = NULL;
  int active_down_threshold_i8 = 0;
  int jump_down_threshold_i8 = 0;
  int floor_skip_frames = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOOOOOOOOOOOOOiii", &action_obj, &action_frame_obj,
                        &char_obj, &on_ground_obj, &pos_x_obj, &pos_y_obj, &speed_y_self_obj,
                        &speed_y_attack_obj, &prev_main_y_obj, &main_y_obj, &platform_h_obj,
                        &platform_valid_obj, &line_id_obj, &platform_id_obj, &rec_x0_obj,
                        &rec_x1_obj, &height_coeff_obj, &local_y_obj, &hard_x0_obj, &hard_y0_obj,
                        &hard_x1_obj, &hard_y1_obj, &active_lut_obj, &attackair_lut_obj,
                        &common_lut_obj, &shallow_lut_obj, &phase_start_obj, &phase_stop_obj,
                        &active_down_threshold_i8, &jump_down_threshold_i8, &floor_skip_frames)) {
    return NULL;
  }

  PyArrayObject* action =
      require_contiguous_array_readonly(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* action_frame =
      require_contiguous_array_readonly(action_frame_obj, NPY_UINT16, 2, "action_frame_u16");
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* on_ground =
      require_contiguous_array_readonly(on_ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* pos_x = require_contiguous_array_readonly(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array_readonly(pos_y_obj, NPY_FLOAT32, 2, "pos_y_f32");
  PyArrayObject* speed_y_self =
      require_contiguous_array_readonly(speed_y_self_obj, NPY_FLOAT32, 2, "speed_y_self_f32");
  PyArrayObject* speed_y_attack =
      require_contiguous_array_readonly(speed_y_attack_obj, NPY_FLOAT32, 2, "speed_y_attack_f32");
  PyArrayObject* prev_main_y =
      require_contiguous_array_readonly(prev_main_y_obj, NPY_INT8, 2, "prev_main_y_i8");
  PyArrayObject* main_y = require_contiguous_array_readonly(main_y_obj, NPY_INT8, 2, "main_y_i8");
  PyArrayObject* platform_h =
      require_contiguous_array_readonly(platform_h_obj, NPY_FLOAT32, 2, "platform_height_f32");
  PyArrayObject* platform_valid = require_contiguous_array_readonly(platform_valid_obj, NPY_UINT8,
                                                                    2, "platform_height_valid_u8");
  PyArrayObject* line_id =
      require_contiguous_array_readonly(line_id_obj, NPY_UINT16, 1, "fod_line_ids_u16");
  PyArrayObject* platform_id =
      require_contiguous_array_readonly(platform_id_obj, NPY_UINT8, 1, "fod_platform_ids_u8");
  PyArrayObject* rec_x0 = require_contiguous_array_readonly(rec_x0_obj, NPY_FLOAT64, 1, "fod_x0");
  PyArrayObject* rec_x1 = require_contiguous_array_readonly(rec_x1_obj, NPY_FLOAT64, 1, "fod_x1");
  PyArrayObject* height_coeff =
      require_contiguous_array_readonly(height_coeff_obj, NPY_FLOAT64, 1, "fod_height_coeff");
  PyArrayObject* local_y =
      require_contiguous_array_readonly(local_y_obj, NPY_FLOAT64, 1, "fod_local_y");
  PyArrayObject* hard_x0 =
      require_contiguous_array_readonly(hard_x0_obj, NPY_FLOAT64, 1, "hard_x0");
  PyArrayObject* hard_y0 =
      require_contiguous_array_readonly(hard_y0_obj, NPY_FLOAT64, 1, "hard_y0");
  PyArrayObject* hard_x1 =
      require_contiguous_array_readonly(hard_x1_obj, NPY_FLOAT64, 1, "hard_x1");
  PyArrayObject* hard_y1 =
      require_contiguous_array_readonly(hard_y1_obj, NPY_FLOAT64, 1, "hard_y1");
  PyArrayObject* active_lut =
      require_contiguous_array_readonly(active_lut_obj, NPY_UINT8, 2, "active_lut_u8");
  PyArrayObject* attackair_lut =
      require_contiguous_array_readonly(attackair_lut_obj, NPY_UINT8, 2, "attackair_lut_u8");
  PyArrayObject* common_lut =
      require_contiguous_array_readonly(common_lut_obj, NPY_UINT8, 2, "common_lut_u8");
  PyArrayObject* shallow_lut =
      require_contiguous_array_readonly(shallow_lut_obj, NPY_UINT8, 2, "shallow_lut_u8");
  PyArrayObject* phase_start =
      require_contiguous_array_readonly(phase_start_obj, NPY_INT16, 2, "phase_start_i16");
  PyArrayObject* phase_stop =
      require_contiguous_array_readonly(phase_stop_obj, NPY_INT16, 2, "phase_stop_i16");
  if (action == NULL || action_frame == NULL || chr == NULL || on_ground == NULL || pos_x == NULL ||
      pos_y == NULL || speed_y_self == NULL || speed_y_attack == NULL || prev_main_y == NULL ||
      main_y == NULL || platform_h == NULL || platform_valid == NULL || line_id == NULL ||
      platform_id == NULL || rec_x0 == NULL || rec_x1 == NULL || height_coeff == NULL ||
      local_y == NULL || hard_x0 == NULL || hard_y0 == NULL || hard_x1 == NULL || hard_y1 == NULL ||
      active_lut == NULL || attackair_lut == NULL || common_lut == NULL || shallow_lut == NULL ||
      phase_start == NULL || phase_stop == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp players = PyArray_DIM(action, 1);
  const npy_intp lut_w = PyArray_DIM(active_lut, 1);
  const npy_intp n_transforms = PyArray_DIM(line_id, 0);
  const npy_intp n_hard = PyArray_DIM(hard_x0, 0);
  if (players <= 0 || players > MSL_MAX_PLAYERS ||
      require_exact_2d_shape(action_frame, n, players, "action_frame_u16") < 0 ||
      require_exact_2d_shape(chr, n, players, "char_id_u8") < 0 ||
      require_exact_2d_shape(on_ground, n, players, "on_ground_u8") < 0 ||
      require_exact_2d_shape(pos_x, n, players, "pos_x_f32") < 0 ||
      require_exact_2d_shape(pos_y, n, players, "pos_y_f32") < 0 ||
      require_exact_2d_shape(speed_y_self, n, players, "speed_y_self_f32") < 0 ||
      require_exact_2d_shape(speed_y_attack, n, players, "speed_y_attack_f32") < 0 ||
      require_exact_2d_shape(prev_main_y, n, players, "prev_main_y_i8") < 0 ||
      require_exact_2d_shape(main_y, n, players, "main_y_i8") < 0 ||
      require_exact_2d_shape(platform_h, n, 2, "platform_height_f32") < 0 ||
      require_exact_2d_shape(platform_valid, n, 2, "platform_height_valid_u8") < 0 ||
      PyArray_DIM(platform_id, 0) != n_transforms || PyArray_DIM(rec_x0, 0) != n_transforms ||
      PyArray_DIM(rec_x1, 0) != n_transforms || PyArray_DIM(height_coeff, 0) != n_transforms ||
      PyArray_DIM(local_y, 0) != n_transforms || PyArray_DIM(hard_y0, 0) != n_hard ||
      PyArray_DIM(hard_x1, 0) != n_hard || PyArray_DIM(hard_y1, 0) != n_hard ||
      PyArray_DIM(active_lut, 0) != 256 || PyArray_DIM(attackair_lut, 0) != 256 ||
      PyArray_DIM(common_lut, 0) != 256 || PyArray_DIM(shallow_lut, 0) != 256 ||
      PyArray_DIM(attackair_lut, 1) != lut_w || PyArray_DIM(common_lut, 1) != lut_w ||
      PyArray_DIM(shallow_lut, 1) != lut_w || PyArray_DIM(phase_start, 0) != 256 ||
      PyArray_DIM(phase_stop, 0) != 256 || PyArray_DIM(phase_start, 1) != lut_w ||
      PyArray_DIM(phase_stop, 1) != lut_w) {
    PyErr_SetString(PyExc_ValueError, "FoD floor-skip inputs have incompatible shapes");
    return NULL;
  }
  if (n_transforms > 64 || n_hard > 64) {
    PyErr_SetString(PyExc_ValueError, "FoD floor-skip stage table exceeds fixed capacity");
    return NULL;
  }

  npy_intp dims[2] = {n, players};
  PyArrayObject* out = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_UINT16);
  if (out == NULL) {
    return NULL;
  }
  uint16_t* outp = (uint16_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n * players; i++) {
    outp[i] = 0xFFFFu;
  }

  MslPyFodLineTransform transforms[64];
  const uint16_t* line_idp = (const uint16_t*)PyArray_DATA(line_id);
  const uint8_t* platform_idp = (const uint8_t*)PyArray_DATA(platform_id);
  const double* rec_x0p = (const double*)PyArray_DATA(rec_x0);
  const double* rec_x1p = (const double*)PyArray_DATA(rec_x1);
  const double* coeffp = (const double*)PyArray_DATA(height_coeff);
  const double* local_yp = (const double*)PyArray_DATA(local_y);
  for (npy_intp i = 0; i < n_transforms; i++) {
    if (platform_idp[i] >= 2u) {
      Py_DECREF(out);
      PyErr_SetString(PyExc_ValueError, "FoD platform id must be 0 or 1");
      return NULL;
    }
    transforms[i].line_id = line_idp[i];
    transforms[i].platform_id = platform_idp[i];
    transforms[i].x0 = rec_x0p[i];
    transforms[i].x1 = rec_x1p[i];
    transforms[i].height_coeff = coeffp[i];
    transforms[i].local_y = local_yp[i];
  }
  MslPyFodHardFloor hard_floors[64];
  const double* hard_x0p = (const double*)PyArray_DATA(hard_x0);
  const double* hard_y0p = (const double*)PyArray_DATA(hard_y0);
  const double* hard_x1p = (const double*)PyArray_DATA(hard_x1);
  const double* hard_y1p = (const double*)PyArray_DATA(hard_y1);
  for (npy_intp i = 0; i < n_hard; i++) {
    hard_floors[i].x0 = hard_x0p[i];
    hard_floors[i].y0 = hard_y0p[i];
    hard_floors[i].x1 = hard_x1p[i];
    hard_floors[i].y1 = hard_y1p[i];
  }

  const uint16_t* actionp = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* framep = (const uint16_t*)PyArray_DATA(action_frame);
  const uint8_t* charp = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* groundp = (const uint8_t*)PyArray_DATA(on_ground);
  const float* pos_xp = (const float*)PyArray_DATA(pos_x);
  const float* pos_yp = (const float*)PyArray_DATA(pos_y);
  const float* speed_selfp = (const float*)PyArray_DATA(speed_y_self);
  const float* speed_attackp = (const float*)PyArray_DATA(speed_y_attack);
  const int8_t* prev_mainp = (const int8_t*)PyArray_DATA(prev_main_y);
  const int8_t* mainp = (const int8_t*)PyArray_DATA(main_y);
  const float* platform_hp = (const float*)PyArray_DATA(platform_h);
  const uint8_t* platform_validp = (const uint8_t*)PyArray_DATA(platform_valid);
  const uint8_t* active_lutp = (const uint8_t*)PyArray_DATA(active_lut);
  const uint8_t* attackair_lutp = (const uint8_t*)PyArray_DATA(attackair_lut);
  const uint8_t* common_lutp = (const uint8_t*)PyArray_DATA(common_lut);
  const uint8_t* shallow_lutp = (const uint8_t*)PyArray_DATA(shallow_lut);
  const int16_t* phase_startp = (const int16_t*)PyArray_DATA(phase_start);
  const int16_t* phase_stopp = (const int16_t*)PyArray_DATA(phase_stop);
  const double jump_skip_root_clearance = (double)(floor_skip_frames < 0 ? 0 : floor_skip_frames);
  const double active_root_clearance = jump_skip_root_clearance + FOD_SKIP_ECB_VERTICAL_UNIT;
  const double shallow_crossing_depth = FOD_SKIP_SHALLOW_ECB_UNITS * FOD_SKIP_ECB_VERTICAL_UNIT;
  uint16_t active_skip[MSL_MAX_PLAYERS];
  int active_remaining[MSL_MAX_PLAYERS];
  uint8_t shallow_carry[MSL_MAX_PLAYERS];
  for (npy_intp p = 0; p < players; p++) {
    active_skip[p] = 0xFFFFu;
    active_remaining[p] = 0;
    shallow_carry[p] = 0u;
  }

  for (npy_intp fi = 0; fi < n; fi++) {
    for (npy_intp slot = 0; slot < players; slot++) {
      const npy_intp idx = fi * players + slot;
      if (groundp[idx] != 0u) {
        active_skip[slot] = 0xFFFFu;
        shallow_carry[slot] = 0u;
        continue;
      }
      const uint16_t action_id = actionp[idx];
      const uint8_t cid = charp[idx];
      const uint8_t active_action = msl_py_fod_lut_u8(active_lutp, lut_w, cid, action_id) != 0u;
      const uint8_t common_action = msl_py_fod_lut_u8(common_lutp, lut_w, cid, action_id) != 0u;
      const uint8_t shallow_action = msl_py_fod_lut_u8(shallow_lutp, lut_w, cid, action_id) != 0u;
      const uint8_t attackair_action =
          msl_py_fod_lut_u8(attackair_lutp, lut_w, cid, action_id) != 0u;
      if (!active_action && !common_action) {
        active_skip[slot] = 0xFFFFu;
        active_remaining[slot] = 0;
        shallow_carry[slot] = 0u;
        continue;
      }
      const bool down_held = active_action ? ((int)mainp[idx] <= active_down_threshold_i8 &&
                                              (int)prev_mainp[idx] <= active_down_threshold_i8)
                                           : ((int)mainp[idx] <= jump_down_threshold_i8 ||
                                              (int)prev_mainp[idx] <= jump_down_threshold_i8);

      if (active_skip[slot] != 0xFFFFu) {
        const uint16_t line = active_skip[slot];
        const MslPyFodLineTransform* rec =
            msl_py_find_fod_line_transform(transforms, n_transforms, line);
        if (active_action) {
          if (rec != NULL && shallow_action) {
            const int16_t start = msl_py_fod_lut_i16(phase_startp, lut_w, cid, action_id);
            const int16_t stop = msl_py_fod_lut_i16(phase_stopp, lut_w, cid, action_id);
            const bool first_phase =
                start >= 0 && (int)framep[idx] >= start && (int)framep[idx] < stop;
            if (first_phase) {
              const int pid = (int)rec->platform_id;
              const double x = (double)pos_xp[idx];
              if (platform_validp[fi * 2 + pid] != 0u &&
                  x >= fmin(rec->x0, rec->x1) - FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP &&
                  x <= fmax(rec->x0, rec->x1) + FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP) {
                const double world_y =
                    rec->local_y + (double)platform_hp[fi * 2 + pid] * rec->height_coeff;
                const double y0 = (double)pos_yp[idx];
                const double y1 = y0 + (double)speed_selfp[idx] + (double)speed_attackp[idx];
                const double prev_depth = world_y - y0;
                if (prev_depth > FOD_FLOOR_Y_BIAS && prev_depth <= shallow_crossing_depth &&
                    y1 < world_y) {
                  shallow_carry[slot] = 1u;
                }
              }
            }
          }
          bool root_clear = false;
          if (rec != NULL) {
            const int pid = (int)rec->platform_id;
            const double x = (double)pos_xp[idx];
            if (platform_validp[fi * 2 + pid] != 0u && pos_yp[idx] > 0.0f &&
                x >= fmin(rec->x0, rec->x1) - active_root_clearance &&
                x <= fmax(rec->x0, rec->x1) + active_root_clearance) {
              const double world_y =
                  rec->local_y + (double)platform_hp[fi * 2 + pid] * rec->height_coeff;
              root_clear = (double)pos_yp[idx] < world_y - active_root_clearance;
            }
          }
          if ((down_held || shallow_carry[slot] != 0u) && root_clear) {
            outp[idx] = line;
          } else {
            const double x = (double)pos_xp[idx];
            const double y0 = (double)pos_yp[idx];
            const double y1 = y0 + (double)speed_selfp[idx] + (double)speed_attackp[idx];
            if (msl_py_fod_hard_floor_root_crossing(hard_floors, n_hard, x, y0, y1)) {
              outp[idx] = line;
              active_skip[slot] = 0xFFFFu;
              active_remaining[slot] = 0;
              shallow_carry[slot] = 0u;
            }
          }
          continue;
        }

        bool jump_below_root = false;
        if (rec != NULL) {
          const int pid = (int)rec->platform_id;
          if (platform_validp[fi * 2 + pid] != 0u) {
            const double world_y =
                rec->local_y + (double)platform_hp[fi * 2 + pid] * rec->height_coeff;
            jump_below_root = (double)pos_yp[idx] <= world_y - jump_skip_root_clearance;
          }
        }
        if (down_held) {
          active_remaining[slot] = floor_skip_frames;
          if (jump_below_root) {
            outp[idx] = line;
          }
          continue;
        }
        if (active_remaining[slot] > 0) {
          if (jump_below_root) {
            outp[idx] = line;
          }
          active_remaining[slot]--;
          continue;
        }
        active_skip[slot] = 0xFFFFu;
        shallow_carry[slot] = 0u;
      }

      const double x = (double)pos_xp[idx];
      const double y0 = (double)pos_yp[idx];
      const double y1 = y0 + (double)speed_selfp[idx] + (double)speed_attackp[idx];
      if (y1 > y0) {
        continue;
      }
      for (npy_intp ti = 0; ti < n_transforms; ti++) {
        const MslPyFodLineTransform* rec = &transforms[ti];
        const int pid = (int)rec->platform_id;
        if (platform_validp[fi * 2 + pid] == 0u) {
          continue;
        }
        if (x < fmin(rec->x0, rec->x1) - FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP ||
            x > fmax(rec->x0, rec->x1) + FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP) {
          continue;
        }
        const double world_y = rec->local_y + (double)platform_hp[fi * 2 + pid] * rec->height_coeff;
        if (down_held && y0 >= world_y - FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP &&
            y1 <= world_y + FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP) {
          active_skip[slot] = rec->line_id;
          active_remaining[slot] = floor_skip_frames;
          shallow_carry[slot] = 0u;
          if (active_action) {
            const bool endpoint_contact =
                fmin(fabs(x - rec->x0), fabs(x - rec->x1)) <= active_root_clearance;
            if (!attackair_action || endpoint_contact) {
              outp[idx] = rec->line_id;
            }
          } else if ((double)pos_yp[idx] <= world_y - jump_skip_root_clearance) {
            outp[idx] = rec->line_id;
          }
          break;
        }
        if (shallow_action) {
          const int16_t start = msl_py_fod_lut_i16(phase_startp, lut_w, cid, action_id);
          const int16_t stop = msl_py_fod_lut_i16(phase_stopp, lut_w, cid, action_id);
          const bool first_phase =
              start >= 0 && (int)framep[idx] >= start && (int)framep[idx] < stop;
          const double prev_depth = world_y - y0;
          if (first_phase && prev_depth > FOD_FLOOR_Y_BIAS &&
              prev_depth <= shallow_crossing_depth && y1 < world_y) {
            active_skip[slot] = rec->line_id;
            active_remaining[slot] = floor_skip_frames;
            shallow_carry[slot] = 1u;
            break;
          }
        }
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_mpcoll_wall_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* line_id_obj = NULL;
  PyObject* kind_obj = NULL;
  PyObject* x0_obj = NULL;
  PyObject* y0_obj = NULL;
  PyObject* x1_obj = NULL;
  PyObject* y1_obj = NULL;
  int stage_id = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOiOOOOOO", &action_obj, &frame_obj, &hitlag_obj, &hitstun_obj,
                        &pos_x_obj, &pos_y_obj, &stage_id, &line_id_obj, &kind_obj, &x0_obj,
                        &y0_obj, &x1_obj, &y1_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* line_id = require_contiguous_array(line_id_obj, NPY_UINT16, 1, "segment_line_id");
  PyArrayObject* kind = require_contiguous_array(kind_obj, NPY_UINT8, 1, "segment_kind");
  PyArrayObject* x0 = require_contiguous_array(x0_obj, NPY_FLOAT32, 1, "segment_x0");
  PyArrayObject* y0 = require_contiguous_array(y0_obj, NPY_FLOAT32, 1, "segment_y0");
  PyArrayObject* x1 = require_contiguous_array(x1_obj, NPY_FLOAT32, 1, "segment_x1");
  PyArrayObject* y1 = require_contiguous_array(y1_obj, NPY_FLOAT32, 1, "segment_y1");
  if (action == NULL || frame == NULL || hitlag == NULL || hitstun == NULL || pos_x == NULL ||
      pos_y == NULL || line_id == NULL || kind == NULL || x0 == NULL || y0 == NULL || x1 == NULL ||
      y1 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n ||
      PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n) {
    PyErr_SetString(PyExc_ValueError, "mpcoll wall seed inputs must have equal lengths");
    return NULL;
  }
  const npy_intp seg_n = PyArray_SIZE(line_id);
  if (PyArray_SIZE(kind) != seg_n || PyArray_SIZE(x0) != seg_n || PyArray_SIZE(y0) != seg_n ||
      PyArray_SIZE(x1) != seg_n || PyArray_SIZE(y1) != seg_n) {
    PyErr_SetString(PyExc_ValueError, "mpcoll segment arrays must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_kind = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_id = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT16, 0);
  if (out_kind == NULL || out_id == NULL) {
    Py_XDECREF(out_kind);
    Py_XDECREF(out_id);
    return NULL;
  }
  uint16_t* oid = (uint16_t*)PyArray_DATA(out_id);
  for (npy_intp i = 0; i < n; i++) oid[i] = 0xFFFFu;
  if (stage_id != 32) {
    return Py_BuildValue("NN", out_kind, out_id);
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const uint16_t* sid = (const uint16_t*)PyArray_DATA(line_id);
  const uint8_t* sk = (const uint8_t*)PyArray_DATA(kind);
  const float* sx0 = (const float*)PyArray_DATA(x0);
  const float* sy0 = (const float*)PyArray_DATA(y0);
  const float* sx1 = (const float*)PyArray_DATA(x1);
  const float* sy1 = (const float*)PyArray_DATA(y1);
  uint8_t* ok = (uint8_t*)PyArray_DATA(out_kind);
  for (npy_intp i = 0; i < n; i++) {
    if (a[i] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP || hl[i] != 0u || hs[i] == 0u || af[i] < 10) {
      continue;
    }
    const float x = px[i];
    const float y = py[i];
    if (!isfinite(x) || !isfinite(y)) continue;
    uint8_t best_kind = 0u;
    uint16_t best_id = 0xFFFFu;
    float best_delta = 7.0f;
    for (npy_intp sidx = 0; sidx < seg_n; sidx++) {
      if (!(sk[sidx] == 2u || sk[sidx] == 3u)) continue;
      const float wall_x =
          msl_py_segment_x_at_y(y, sx0[sidx], sy0[sidx], sx1[sidx], sy1[sidx], true);
      if (!isfinite(wall_x)) continue;
      float delta = 0.0f;
      uint8_t candidate_kind = 0u;
      if (sk[sidx] == 3u) {
        delta = wall_x - x;
        candidate_kind = 1u;
      } else {
        delta = x - wall_x;
        candidate_kind = 2u;
      }
      if (delta < 0.0f || delta > 6.0f) continue;
      if (delta < best_delta) {
        best_delta = delta;
        best_kind = candidate_kind;
        best_id = sid[sidx];
      }
    }
    if (best_kind != 0u) {
      ok[i] = best_kind;
      oid[i] = best_id;
    }
  }
  return Py_BuildValue("NN", out_kind, out_id);
}
