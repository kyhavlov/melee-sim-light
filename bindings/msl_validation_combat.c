/* Native validation combat/history materialization. */

#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY

#include "msl_validation_combat.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/api.h"
#include "../src/attack_id_tables.h"
#include "../src/move_tables.h"
#include "../src/staling.h"

static int require_validation_u8_rows(PyArrayObject* arr, npy_intp rows, npy_intp min_cols,
                                      const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s must be uint8[%zd, >=%zd]", name, rows, min_cols);
    return -1;
  }
  return 0;
}

static int require_min_2d(PyArrayObject* arr, npy_intp rows, npy_intp min_cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s shape mismatch", name);
    return -1;
  }
  return 0;
}

static inline uint16_t inc_attack_instance_counter(uint16_t* counter) {
  const uint16_t before = *counter;
  *counter = (uint16_t)(*counter + 1u);
  if (*counter == 0u) {
    *counter = 1u;
  }
  return before;
}

static inline const MslSeed* seed_row_at(const uint8_t* seed_u8, size_t stride, npy_intp i) {
  return (const MslSeed*)(const void*)(seed_u8 + (size_t)i * stride);
}

static inline MslSeed* mutable_seed_row_at(uint8_t* seed_u8, size_t stride, npy_intp i) {
  return (MslSeed*)(void*)(seed_u8 + (size_t)i * stride);
}

static inline const MslCompare* ref_row_at(const uint8_t* ref_u8, size_t stride, npy_intp i) {
  return (const MslCompare*)(const void*)(ref_u8 + (size_t)i * stride);
}

static inline const MslItem* item_row_at(const uint8_t* item_u8, size_t stride, npy_intp i) {
  return (const MslItem*)(const void*)(item_u8 + (size_t)i * stride);
}

static inline MslItem* mutable_item_row_at(uint8_t* item_u8, size_t stride, npy_intp i) {
  return (MslItem*)(void*)(item_u8 + (size_t)i * stride);
}

static inline uint8_t frame_char(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->char_id[p] : ref->char_id[p];
}

static inline uint16_t frame_action(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->action_id[p] : ref->action_id[p];
}

static inline uint32_t frame_anim(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->animation_index[p] : ref->animation_index[p];
}

static inline float frame_percent(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->percent[p] : ref->percent[p];
}

static inline uint8_t frame_stocks(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->stocks[p] : ref->stocks[p];
}

static inline uint16_t frame_iid(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->instance_id[p] : ref->instance_id[p];
}

static inline uint8_t frame_last_hit_by(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->last_hit_by[p] : ref->last_hit_by[p];
}

static inline uint16_t frame_last_hit_iid(const MslSeed* seed, const MslCompare* ref, int p) {
  return seed != NULL ? seed->instance_hit_by[p] : ref->instance_hit_by[p];
}

typedef struct StalingContext {
  const uint8_t* seed_u8;
  const uint8_t* ref_u8;
  const uint8_t* item_u8;
  size_t seed_stride;
  size_t ref_stride;
  size_t item_stride;
  const float* anim_frame_f32;
  npy_intp anim_width;
  npy_intp n_frames;
  npy_intp n_samples;
  int num_players;
  int slot_by_port0[4];
  uint8_t with_items;
} StalingContext;

static int derive_staling_core(const StalingContext* ctx, uint16_t* out_attack_id,
                               uint16_t* out_attack_inst, uint8_t* out_qi, uint16_t* out_mid,
                               uint16_t* out_inst) {
  uint16_t* map_mid = (uint16_t*)PyMem_Malloc((size_t)ctx->num_players * 65536u * sizeof(uint16_t));
  uint16_t* map_inst =
      (uint16_t*)PyMem_Malloc((size_t)ctx->num_players * 65536u * sizeof(uint16_t));
  if (map_mid == NULL || map_inst == NULL) {
    PyMem_Free(map_mid);
    PyMem_Free(map_inst);
    PyErr_NoMemory();
    return -1;
  }
  for (size_t i = 0; i < (size_t)ctx->num_players * 65536u; i++) {
    map_mid[i] = 0xFFFFu;
    map_inst[i] = 0u;
  }

  uint8_t qi[MSL_MAX_PLAYERS] = {0};
  uint16_t table_mid[MSL_MAX_PLAYERS][MSL_STALE_QUEUE_SIZE] = {{0}};
  uint16_t table_inst[MSL_MAX_PLAYERS][MSL_STALE_QUEUE_SIZE] = {{0}};
  uint16_t cur_attack_id[MSL_MAX_PLAYERS];
  uint16_t cur_attack_inst[MSL_MAX_PLAYERS] = {0};
  uint16_t prev_action_id[MSL_MAX_PLAYERS];
  uint16_t prev_state_iid[MSL_MAX_PLAYERS] = {0};
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    cur_attack_id[p] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
    prev_action_id[p] = 0xFFFFu;
  }
  uint16_t stale_attack_counter = 1u;

#define RESET_STALE_TABLE(P)                             \
  do {                                                   \
    qi[(P)] = 0u;                                        \
    memset(table_mid[(P)], 0, sizeof(table_mid[(P)]));   \
    memset(table_inst[(P)], 0, sizeof(table_inst[(P)])); \
  } while (0)
#define RESET_ATTACK_IDENTITY(P)                           \
  do {                                                     \
    cur_attack_id[(P)] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT; \
    cur_attack_inst[(P)] = 0u;                             \
    prev_action_id[(P)] = 0xFFFFu;                         \
    prev_state_iid[(P)] = 0u;                              \
  } while (0)

  const uint16_t ACT_GUARD_ON = 178u;
  const uint16_t ACT_GUARD = 179u;
  const uint16_t ACT_GUARD_OFF = 180u;
  const uint16_t ACT_FX_SPECIAL_N_LOOP = 0x0156u;
  const uint16_t ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159u;
  const uint32_t NO_SUBMOTION_INDEX = 0xFFFFFFFFu;

  for (npy_intp t = 0; t < ctx->n_frames; t++) {
    const MslSeed* seed =
        t < ctx->n_samples ? seed_row_at(ctx->seed_u8, ctx->seed_stride, t) : NULL;
    const MslCompare* ref =
        t < ctx->n_samples ? NULL : ref_row_at(ctx->ref_u8, ctx->ref_stride, ctx->n_samples - 1);
    const MslSeed* prev_seed = t > 0 && (t - 1) < ctx->n_samples
                                   ? seed_row_at(ctx->seed_u8, ctx->seed_stride, t - 1)
                                   : NULL;
    const MslCompare* prev_ref = (t > 0 && (t - 1) >= ctx->n_samples)
                                     ? ref_row_at(ctx->ref_u8, ctx->ref_stride, ctx->n_samples - 1)
                                     : NULL;

    if (t > 0) {
      for (int p = 0; p < ctx->num_players; p++) {
        if (frame_stocks(seed, ref, p) < frame_stocks(prev_seed, prev_ref, p)) {
          RESET_STALE_TABLE(p);
          RESET_ATTACK_IDENTITY(p);
        }
      }
    }

    for (int p = 0; p < ctx->num_players; p++) {
      const size_t pi = (size_t)t * (size_t)ctx->num_players + (size_t)p;
      const uint16_t iid = frame_iid(seed, ref, p);
      if (iid == 0u) {
        RESET_ATTACK_IDENTITY(p);
        out_attack_id[pi] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
        continue;
      }
      const uint16_t act = frame_action(seed, ref, p);
      const float af = ctx->anim_frame_f32[(size_t)t * (size_t)ctx->anim_width + (size_t)p];
      if (act != prev_action_id[p]) {
        if (act == ACT_GUARD_OFF && prev_action_id[p] == ACT_GUARD_ON && t > 0) {
          const uint16_t prev_act = frame_action(prev_seed, prev_ref, p);
          const uint32_t prev_anim = frame_anim(prev_seed, prev_ref, p);
          const float prev_af =
              ctx->anim_frame_f32[(size_t)(t - 1) * (size_t)ctx->anim_width + (size_t)p];
          if (prev_act == ACT_GUARD_ON && prev_anim == NO_SUBMOTION_INDEX && prev_af < 0.0f) {
            const uint16_t hidden_guard_inst = inc_attack_instance_counter(&stale_attack_counter);
            cur_attack_id[p] = attack_id_move_id_from_action(frame_char(seed, ref, p), ACT_GUARD);
            cur_attack_inst[p] = hidden_guard_inst;
          }
        }

        const uint16_t move_id = attack_id_move_id_from_action(frame_char(seed, ref, p), act);
        if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || move_id != cur_attack_id[p]) {
          cur_attack_id[p] = move_id;
          cur_attack_inst[p] = inc_attack_instance_counter(&stale_attack_counter);
        }
        prev_action_id[p] = act;
      } else if ((act == ACT_FX_SPECIAL_N_LOOP || act == ACT_FX_SPECIAL_AIR_N_LOOP) &&
                 iid != prev_state_iid[p] && af == 0.0f) {
        cur_attack_inst[p] = inc_attack_instance_counter(&stale_attack_counter);
      }

      if (iid != prev_state_iid[p]) {
        const size_t mi = (size_t)p * 65536u + (size_t)iid;
        if (map_mid[mi] == 0xFFFFu && map_inst[mi] == 0u) {
          map_mid[mi] = cur_attack_id[p];
          map_inst[mi] = cur_attack_inst[p];
        }
        prev_state_iid[p] = iid;
      }
      out_attack_inst[pi] = cur_attack_inst[p];
      out_attack_id[pi] = cur_attack_id[p];
    }

    if (t > 0) {
      const MslItem* prev_items = item_row_at(ctx->item_u8, ctx->item_stride, t - 1);
      const MslItem* cur_items = item_row_at(ctx->item_u8, ctx->item_stride, t);
      for (int victim = 0; victim < ctx->num_players; victim++) {
        const float dp =
            frame_percent(seed, ref, victim) - frame_percent(prev_seed, prev_ref, victim);
        if (!(dp > 0.0f)) {
          continue;
        }
        int attacker = -1;
        uint16_t att_move_id = 0xFFFFu;
        uint16_t att_attack_inst = 0u;
        const uint16_t hit_iid = frame_last_hit_iid(seed, ref, victim);
        const uint8_t port0 = frame_last_hit_by(seed, ref, victim);
        if (port0 < 4u) {
          attacker = ctx->slot_by_port0[port0];
        }
        if (attacker < 0 && hit_iid != 0u) {
          int match = -1;
          int match_count = 0;
          for (int p = 0; p < ctx->num_players; p++) {
            if (frame_iid(seed, ref, p) == hit_iid) {
              match = p;
              match_count++;
            }
          }
          if (match_count == 1) {
            attacker = match;
          }
        }

        uint8_t hit_iid_matches_fighter_row = 0u;
        if (hit_iid != 0u) {
          for (int p = 0; p < ctx->num_players; p++) {
            if (frame_iid(seed, ref, p) == hit_iid) {
              if (port0 >= 4u || ctx->slot_by_port0[port0] < 0 || ctx->slot_by_port0[port0] == p) {
                hit_iid_matches_fighter_row = 1u;
              }
              break;
            }
          }
        }

        if (attacker >= 0 && hit_iid != 0u) {
          const size_t mi = (size_t)attacker * 65536u + (size_t)hit_iid;
          if (map_mid[mi] != 0xFFFFu || map_inst[mi] != 0u) {
            att_move_id = map_mid[mi];
            att_attack_inst = map_inst[mi];
          }
        }

        const uint8_t fighter_hit_identity_available =
            (attacker >= 0 && hit_iid_matches_fighter_row != 0u &&
             ((att_move_id != 0xFFFFu && att_move_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
               att_attack_inst != 0u) ||
              (cur_attack_id[attacker] != 0xFFFFu &&
               cur_attack_id[attacker] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
               cur_attack_inst[attacker] != 0u)))
                ? 1u
                : 0u;

        if (ctx->with_items != 0u && hit_iid != 0u && fighter_hit_identity_available == 0u &&
            (att_move_id == 0xFFFFu || att_move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
             att_attack_inst == 0u)) {
          int item_attacker = -1;
          uint16_t item_move_id = 0xFFFFu;
          uint16_t item_attack_inst = 0u;
          int item_match_count = 0;
          for (int pass = 0; pass < 2; pass++) {
            const MslItem* row = pass == 0 ? prev_items : cur_items;
            for (int it = 0; it < MSL_MAX_ITEMS; it++) {
              const MslItem* item = &row[it];
              if (item->exists == 0u || item->instance_id != hit_iid) {
                continue;
              }
              const int owner = (int)item->owner;
              if (owner < 0 || owner >= ctx->num_players || owner == victim) {
                continue;
              }
              if (port0 < 4u && ctx->slot_by_port0[port0] >= 0 &&
                  ctx->slot_by_port0[port0] != owner) {
                continue;
              }
              item_attacker = owner;
              item_move_id = item->attack_id;
              item_attack_inst = item->attack_instance;
              item_match_count++;
            }
            if (item_match_count > 0) {
              break;
            }
          }
          if (item_match_count == 1) {
            attacker = item_attacker;
            att_move_id = item_move_id;
            att_attack_inst = item_attack_inst;
          }
        }
        if (attacker < 0 || attacker == victim) {
          continue;
        }

        if ((att_move_id == 0xFFFFu || att_attack_inst == 0u) && hit_iid != 0u) {
          const size_t mi = (size_t)attacker * 65536u + (size_t)hit_iid;
          if (map_mid[mi] != 0xFFFFu || map_inst[mi] != 0u) {
            att_move_id = map_mid[mi];
            att_attack_inst = map_inst[mi];
          }
        }
        if (att_move_id == 0xFFFFu || att_attack_inst == 0u) {
          att_move_id = cur_attack_id[attacker];
          att_attack_inst = cur_attack_inst[attacker];
        }
        if (att_move_id == 0xFFFFu || att_move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
            att_attack_inst == 0u) {
          continue;
        }
        uint8_t duplicate = 0u;
        for (int k = 0; k < MSL_STALE_QUEUE_SIZE; k++) {
          if (table_mid[attacker][k] == att_move_id && table_inst[attacker][k] == att_attack_inst) {
            duplicate = 1u;
            break;
          }
        }
        if (!duplicate) {
          int pos = qi[attacker];
          if (pos >= MSL_STALE_QUEUE_SIZE) {
            pos = 0;
          }
          table_mid[attacker][pos] = att_move_id;
          table_inst[attacker][pos] = att_attack_inst;
          qi[attacker] = (uint8_t)(pos == (MSL_STALE_QUEUE_SIZE - 1) ? 0 : pos + 1);
        }
      }
    }

    for (int p = 0; p < ctx->num_players; p++) {
      const size_t pi = (size_t)t * (size_t)ctx->num_players + (size_t)p;
      out_qi[pi] = qi[p];
      for (int k = 0; k < MSL_STALE_QUEUE_SIZE; k++) {
        const size_t oi =
            ((size_t)t * (size_t)ctx->num_players + (size_t)p) * MSL_STALE_QUEUE_SIZE + (size_t)k;
        out_mid[oi] = table_mid[p][k];
        out_inst[oi] = table_inst[p][k];
      }
    }
  }

#undef RESET_STALE_TABLE
#undef RESET_ATTACK_IDENTITY
  PyMem_Free(map_mid);
  PyMem_Free(map_inst);
  return 0;
}

static void derive_item_attack_fields_inplace(uint8_t* item_u8, size_t item_stride,
                                              npy_intp n_frames, const uint16_t* fighter_attack_id,
                                              const uint16_t* fighter_attack_instance,
                                              int num_players, const uint16_t* prev_kind,
                                              npy_intp prev_kind_count) {
  uint32_t active_spawn[MSL_MAX_ITEMS] = {0};
  uint16_t active_type[MSL_MAX_ITEMS] = {0};
  uint16_t active_aid[MSL_MAX_ITEMS] = {0};
  uint16_t active_ainst[MSL_MAX_ITEMS] = {0};
  bool active_valid[MSL_MAX_ITEMS] = {false};
  for (npy_intp i = 0; i < n_frames; i++) {
    bool seen[MSL_MAX_ITEMS] = {false};
    MslItem* row = mutable_item_row_at(item_u8, item_stride, i);
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      MslItem* item = &row[slot];
      if (item->exists == 0u) {
        item->attack_id = 0u;
        item->attack_instance = 0u;
        continue;
      }
      const uint32_t key_spawn = item->spawn_id;
      const uint16_t key_type = item->type;
      int active_idx = -1;
      for (int k = 0; k < MSL_MAX_ITEMS; k++) {
        if (active_valid[k] && active_spawn[k] == key_spawn && active_type[k] == key_type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < MSL_MAX_ITEMS; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) {
          active_idx = 0;
        }
        active_valid[active_idx] = true;
        active_spawn[active_idx] = key_spawn;
        active_type[active_idx] = key_type;
        const int owner = (int)item->owner;
        if (owner >= 0 && owner < num_players) {
          npy_intp owner_frame = i;
          uint8_t use_prev_owner_frame = 0u;
          if (prev_kind != NULL && i > 0) {
            for (npy_intp k = 0; k < prev_kind_count; k++) {
              if (prev_kind[k] == key_type) {
                use_prev_owner_frame = 1u;
                break;
              }
            }
          }
          if (use_prev_owner_frame != 0u) {
            const size_t cur_owner_idx = (size_t)i * (size_t)num_players + (size_t)owner;
            const size_t prev_owner_idx = (size_t)(i - 1) * (size_t)num_players + (size_t)owner;
            if ((fighter_attack_id[cur_owner_idx] == 1u ||
                 fighter_attack_instance[cur_owner_idx] == 0u) &&
                fighter_attack_id[prev_owner_idx] != 1u &&
                fighter_attack_instance[prev_owner_idx] != 0u) {
              owner_frame = i - 1;
            }
          }
          const size_t owner_idx = (size_t)owner_frame * (size_t)num_players + (size_t)owner;
          active_aid[active_idx] = fighter_attack_id[owner_idx];
          active_ainst[active_idx] = fighter_attack_instance[owner_idx];
        } else {
          active_aid[active_idx] = 1u;
          active_ainst[active_idx] = 0u;
        }
      }
      seen[active_idx] = true;
      item->attack_id = active_aid[active_idx];
      item->attack_instance = active_ainst[active_idx];
    }
    for (int k = 0; k < MSL_MAX_ITEMS; k++) {
      if (active_valid[k] && !seen[k]) {
        active_valid[k] = false;
      }
    }
  }
}

static void fill_seed_staling(uint8_t* seed_u8, size_t seed_stride, npy_intp n_samples,
                              int num_players, const uint16_t* attack_id,
                              const uint16_t* attack_inst, const uint8_t* qi, const uint16_t* mid,
                              const uint16_t* inst) {
  for (npy_intp i = 0; i < n_samples; i++) {
    MslSeed* seed = mutable_seed_row_at(seed_u8, seed_stride, i);
    for (int p = 0; p < num_players; p++) {
      const size_t pi = (size_t)i * (size_t)num_players + (size_t)p;
      seed->attack_id[p] = attack_id[pi];
      seed->attack_instance[p] = attack_inst[pi];
      seed->stale_queue_index[p] = qi[pi];
      for (int k = 0; k < MSL_STALE_QUEUE_SIZE; k++) {
        const size_t oi =
            ((size_t)i * (size_t)num_players + (size_t)p) * MSL_STALE_QUEUE_SIZE + (size_t)k;
        seed->stale_move_id[p][k] = mid[oi];
        seed->stale_attack_instance[p][k] = inst[oi];
      }
    }
  }
}

PyObject* msl_validation_derive_staling_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* prev_kind_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOi", &seed_obj, &ref_obj, &items_obj, &anim_frame_obj,
                        &prev_kind_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* items_arr = require_contiguous_array(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* anim_frame =
      require_contiguous_array_readonly(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* prev_kind = NULL;
  if (prev_kind_obj != NULL && prev_kind_obj != Py_None) {
    prev_kind = require_contiguous_array_readonly(prev_kind_obj, NPY_UINT16, 1, "prev_kind_u16");
  }
  if (seed_arr == NULL || ref_arr == NULL || items_arr == NULL || anim_frame == NULL ||
      (prev_kind_obj != NULL && prev_kind_obj != Py_None && prev_kind == NULL)) {
    return NULL;
  }
  const npy_intp n_samples = PyArray_DIM(seed_arr, 0);
  const npy_intp n_frames = n_samples + 1;
  if (n_samples < 1 || num_players <= 0 || num_players > MSL_MAX_PLAYERS ||
      require_validation_u8_rows(seed_arr, n_samples, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_validation_u8_rows(ref_arr, n_samples, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      require_validation_u8_rows(items_arr, n_frames, (npy_intp)(sizeof(MslItem) * MSL_MAX_ITEMS),
                                 "items_u8") != 0 ||
      require_min_2d(anim_frame, n_frames, num_players, "anim_frame_f32") != 0) {
    return NULL;
  }

  if (attack_id_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "attack_id_tables_init failed");
    return NULL;
  }

  npy_intp dims2[2] = {n_frames, num_players};
  npy_intp dims3[3] = {n_frames, num_players, MSL_STALE_QUEUE_SIZE};
  PyArrayObject* initial_attack_id = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_UINT16, 0);
  PyArrayObject* initial_attack_inst = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* initial_qi = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* initial_mid = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  PyArrayObject* initial_inst = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  PyArrayObject* out_attack_id = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_attack_inst = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_qi = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* out_mid = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  PyArrayObject* out_inst = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  if (initial_attack_id == NULL || initial_attack_inst == NULL || initial_qi == NULL ||
      initial_mid == NULL || initial_inst == NULL || out_attack_id == NULL ||
      out_attack_inst == NULL || out_qi == NULL || out_mid == NULL || out_inst == NULL) {
    Py_XDECREF(initial_attack_id);
    Py_XDECREF(initial_attack_inst);
    Py_XDECREF(initial_qi);
    Py_XDECREF(initial_mid);
    Py_XDECREF(initial_inst);
    Py_XDECREF(out_attack_id);
    Py_XDECREF(out_attack_inst);
    Py_XDECREF(out_qi);
    Py_XDECREF(out_mid);
    Py_XDECREF(out_inst);
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* ref_u8 = (const uint8_t*)PyArray_DATA(ref_arr);
  uint8_t* items_u8 = (uint8_t*)PyArray_DATA(items_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  const size_t item_stride = (size_t)PyArray_STRIDE(items_arr, 0);

  int slot_by_port0[4] = {-1, -1, -1, -1};
  const MslSeed* first_seed = seed_row_at(seed_u8, seed_stride, 0);
  for (int p = 0; p < num_players; p++) {
    if (first_seed->source_port0[p] < 4u) {
      slot_by_port0[first_seed->source_port0[p]] = p;
    }
  }

  StalingContext ctx = {
      .seed_u8 = seed_u8,
      .ref_u8 = ref_u8,
      .item_u8 = items_u8,
      .seed_stride = seed_stride,
      .ref_stride = ref_stride,
      .item_stride = item_stride,
      .anim_frame_f32 = (const float*)PyArray_DATA(anim_frame),
      .anim_width = PyArray_DIM(anim_frame, 1),
      .n_frames = n_frames,
      .n_samples = n_samples,
      .num_players = num_players,
      .slot_by_port0 = {slot_by_port0[0], slot_by_port0[1], slot_by_port0[2], slot_by_port0[3]},
      .with_items = 0u,
  };

  if (derive_staling_core(&ctx, (uint16_t*)PyArray_DATA(initial_attack_id),
                          (uint16_t*)PyArray_DATA(initial_attack_inst),
                          (uint8_t*)PyArray_DATA(initial_qi), (uint16_t*)PyArray_DATA(initial_mid),
                          (uint16_t*)PyArray_DATA(initial_inst)) != 0) {
    Py_DECREF(initial_attack_id);
    Py_DECREF(initial_attack_inst);
    Py_DECREF(initial_qi);
    Py_DECREF(initial_mid);
    Py_DECREF(initial_inst);
    Py_DECREF(out_attack_id);
    Py_DECREF(out_attack_inst);
    Py_DECREF(out_qi);
    Py_DECREF(out_mid);
    Py_DECREF(out_inst);
    return NULL;
  }

  derive_item_attack_fields_inplace(
      items_u8, item_stride, n_frames, (const uint16_t*)PyArray_DATA(initial_attack_id),
      (const uint16_t*)PyArray_DATA(initial_attack_inst), num_players,
      prev_kind != NULL ? (const uint16_t*)PyArray_DATA(prev_kind) : NULL,
      prev_kind != NULL ? PyArray_SIZE(prev_kind) : 0);

  ctx.with_items = 1u;
  if (derive_staling_core(&ctx, (uint16_t*)PyArray_DATA(out_attack_id),
                          (uint16_t*)PyArray_DATA(out_attack_inst), (uint8_t*)PyArray_DATA(out_qi),
                          (uint16_t*)PyArray_DATA(out_mid),
                          (uint16_t*)PyArray_DATA(out_inst)) != 0) {
    Py_DECREF(initial_attack_id);
    Py_DECREF(initial_attack_inst);
    Py_DECREF(initial_qi);
    Py_DECREF(initial_mid);
    Py_DECREF(initial_inst);
    Py_DECREF(out_attack_id);
    Py_DECREF(out_attack_inst);
    Py_DECREF(out_qi);
    Py_DECREF(out_mid);
    Py_DECREF(out_inst);
    return NULL;
  }
  fill_seed_staling(
      seed_u8, seed_stride, n_samples, num_players, (const uint16_t*)PyArray_DATA(out_attack_id),
      (const uint16_t*)PyArray_DATA(out_attack_inst), (const uint8_t*)PyArray_DATA(out_qi),
      (const uint16_t*)PyArray_DATA(out_mid), (const uint16_t*)PyArray_DATA(out_inst));

  Py_DECREF(initial_attack_id);
  Py_DECREF(initial_attack_inst);
  Py_DECREF(initial_qi);
  Py_DECREF(initial_mid);
  Py_DECREF(initial_inst);
  return Py_BuildValue("NNNNN", out_attack_id, out_attack_inst, out_qi, out_mid, out_inst);
}
