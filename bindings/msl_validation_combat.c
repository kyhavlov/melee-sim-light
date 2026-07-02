/* Native validation combat/history materialization. */

#include "msl_validation_combat.h"
#include "msl_preprocess.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/api.h"
#include "../src/action_ids.h"
#include "../src/anim_pose.h"
#include "../src/anim_table.h"
#include "../src/char_params.h"
#include "../src/combat_geom.h"
#include "../src/common_params.h"
#include "../src/hit_elements.h"
#include "../src/hitboxes_tables.h"
#include "../src/hurtcaps_tables.h"
#include "../src/ids.h"
#include "../src/attack_id_tables.h"
#include "../src/move_tables.h"
#include "../src/shield_tilt_table.h"
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

/* Preprocess entrypoints owned by this validation family. */

static inline uint16_t msl_py_inc_attack_instance_counter(uint16_t* counter) {
  const uint16_t before = *counter;
  *counter = (uint16_t)(*counter + 1u);
  if (*counter == 0u) {
    *counter = 1u;
  }
  return before;
}

PyObject* msl_derive_staling_history_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* src_ports_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* state_iid_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* last_hit_by_iid_obj = NULL;
  PyObject* item_exists_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_instance_id_obj = NULL;
  PyObject* item_attack_id_obj = NULL;
  PyObject* item_attack_instance_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOO|OOOOO", &src_ports_obj, &char_obj, &action_obj,
                        &action_frame_obj, &anim_obj, &percent_obj, &stocks_obj, &state_iid_obj,
                        &last_hit_by_obj, &last_hit_by_iid_obj, &item_exists_obj, &item_owner_obj,
                        &item_instance_id_obj, &item_attack_id_obj, &item_attack_instance_obj)) {
    return NULL;
  }
  PyObject* src_seq = PySequence_Fast(src_ports_obj, "src_ports must be a sequence");
  if (src_seq == NULL) {
    return NULL;
  }
  const Py_ssize_t num_players_ssize = PySequence_Fast_GET_SIZE(src_seq);
  if (num_players_ssize <= 0 || num_players_ssize > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "src_ports length must be in 1..4");
    Py_DECREF(src_seq);
    return NULL;
  }
  const int num_players = (int)num_players_ssize;
  int slot_by_port0[4] = {-1, -1, -1, -1};
  for (int i = 0; i < num_players; i++) {
    const long port1 = PyLong_AsLong(PySequence_Fast_GET_ITEM(src_seq, i));
    if (PyErr_Occurred()) {
      Py_DECREF(src_seq);
      return NULL;
    }
    if (port1 < 1 || port1 > 4) {
      PyErr_SetString(PyExc_ValueError, "src_ports must be in 1..4");
      Py_DECREF(src_seq);
      return NULL;
    }
    slot_by_port0[(int)port1 - 1] = i;
  }
  Py_DECREF(src_seq);

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_FLOAT32, 2, "action_frame");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 2, "animation_index");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 2, "percent");
  PyArrayObject* stocks = require_contiguous_array(stocks_obj, NPY_UINT8, 2, "stocks");
  PyArrayObject* state_iid = require_contiguous_array(state_iid_obj, NPY_UINT16, 2, "instance_id");
  PyArrayObject* last_hit_by =
      require_contiguous_array(last_hit_by_obj, NPY_UINT8, 2, "last_hit_by");
  PyArrayObject* last_hit_by_iid =
      require_contiguous_array(last_hit_by_iid_obj, NPY_UINT16, 2, "last_hit_by_instance");
  PyArrayObject* item_exists = NULL;
  PyArrayObject* item_owner = NULL;
  PyArrayObject* item_instance_id = NULL;
  PyArrayObject* item_attack_id = NULL;
  PyArrayObject* item_attack_instance = NULL;
  const uint8_t have_item_args =
      (item_exists_obj != NULL || item_owner_obj != NULL || item_instance_id_obj != NULL ||
       item_attack_id_obj != NULL || item_attack_instance_obj != NULL)
          ? 1u
          : 0u;
  if (have_item_args) {
    if (item_exists_obj == NULL || item_owner_obj == NULL || item_instance_id_obj == NULL ||
        item_attack_id_obj == NULL || item_attack_instance_obj == NULL) {
      PyErr_SetString(PyExc_ValueError, "item staling inputs must be provided as a complete set");
      return NULL;
    }
    item_exists = require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists");
    item_owner = require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner");
    item_instance_id =
        require_contiguous_array(item_instance_id_obj, NPY_UINT16, 2, "item_instance_id");
    item_attack_id = require_contiguous_array(item_attack_id_obj, NPY_UINT16, 2, "item_attack_id");
    item_attack_instance =
        require_contiguous_array(item_attack_instance_obj, NPY_UINT16, 2, "item_attack_instance");
  }
  if (char_id == NULL || action_id == NULL || action_frame == NULL || anim == NULL ||
      percent == NULL || stocks == NULL || state_iid == NULL || last_hit_by == NULL ||
      last_hit_by_iid == NULL ||
      (have_item_args && (item_exists == NULL || item_owner == NULL || item_instance_id == NULL ||
                          item_attack_id == NULL || item_attack_instance == NULL))) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action_id, 0);
  const npy_intp w = PyArray_DIM(action_id, 1);
  if (w != num_players) {
    PyErr_SetString(PyExc_ValueError, "staling arrays must have width equal to len(src_ports)");
    return NULL;
  }
  if (require_exact_2d_shape(char_id, n, w, "char_id") != 0 ||
      require_exact_2d_shape(action_frame, n, w, "action_frame") != 0 ||
      require_exact_2d_shape(anim, n, w, "animation_index") != 0 ||
      require_exact_2d_shape(percent, n, w, "percent") != 0 ||
      require_exact_2d_shape(stocks, n, w, "stocks") != 0 ||
      require_exact_2d_shape(state_iid, n, w, "instance_id") != 0 ||
      require_exact_2d_shape(last_hit_by, n, w, "last_hit_by") != 0 ||
      require_exact_2d_shape(last_hit_by_iid, n, w, "last_hit_by_instance") != 0) {
    return NULL;
  }
  npy_intp item_width = 0;
  if (have_item_args) {
    item_width = PyArray_DIM(item_exists, 1);
    if (PyArray_NDIM(item_exists) != 2 || PyArray_DIM(item_exists, 0) != n ||
        PyArray_NDIM(item_owner) != 2 || PyArray_DIM(item_owner, 0) != n ||
        PyArray_DIM(item_owner, 1) != item_width || PyArray_NDIM(item_instance_id) != 2 ||
        PyArray_DIM(item_instance_id, 0) != n || PyArray_DIM(item_instance_id, 1) != item_width ||
        PyArray_NDIM(item_attack_id) != 2 || PyArray_DIM(item_attack_id, 0) != n ||
        PyArray_DIM(item_attack_id, 1) != item_width || PyArray_NDIM(item_attack_instance) != 2 ||
        PyArray_DIM(item_attack_instance, 0) != n ||
        PyArray_DIM(item_attack_instance, 1) != item_width) {
      PyErr_SetString(PyExc_ValueError, "item staling inputs must have matching [n,item] shapes");
      return NULL;
    }
  }
  if (attack_id_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "attack_id_tables_init failed");
    return NULL;
  }

  npy_intp dims2[2] = {n, w};
  npy_intp dims3[3] = {n, w, 10};
  PyArrayObject* out_attack_id = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_attack_inst = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_qi = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* out_mid = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  PyArrayObject* out_inst = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  if (out_attack_id == NULL || out_attack_inst == NULL || out_qi == NULL || out_mid == NULL ||
      out_inst == NULL) {
    Py_XDECREF(out_attack_id);
    Py_XDECREF(out_attack_inst);
    Py_XDECREF(out_qi);
    Py_XDECREF(out_mid);
    Py_XDECREF(out_inst);
    return NULL;
  }

  uint16_t* map_mid = (uint16_t*)PyMem_Malloc((size_t)num_players * 65536u * sizeof(uint16_t));
  uint16_t* map_inst = (uint16_t*)PyMem_Malloc((size_t)num_players * 65536u * sizeof(uint16_t));
  if (map_mid == NULL || map_inst == NULL) {
    PyMem_Free(map_mid);
    PyMem_Free(map_inst);
    Py_DECREF(out_attack_id);
    Py_DECREF(out_attack_inst);
    Py_DECREF(out_qi);
    Py_DECREF(out_mid);
    Py_DECREF(out_inst);
    return PyErr_NoMemory();
  }
  for (size_t i = 0; i < (size_t)num_players * 65536u; i++) {
    map_mid[i] = 0xFFFFu;
    map_inst[i] = 0u;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const float* frame_p = (const float*)PyArray_DATA(action_frame);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const float* percent_p = (const float*)PyArray_DATA(percent);
  const uint8_t* stocks_p = (const uint8_t*)PyArray_DATA(stocks);
  const uint16_t* iid_p = (const uint16_t*)PyArray_DATA(state_iid);
  const uint8_t* last_hit_p = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint16_t* last_hit_iid_p = (const uint16_t*)PyArray_DATA(last_hit_by_iid);
  const uint8_t* item_exists_p = have_item_args ? (const uint8_t*)PyArray_DATA(item_exists) : NULL;
  const int8_t* item_owner_p = have_item_args ? (const int8_t*)PyArray_DATA(item_owner) : NULL;
  const uint16_t* item_iid_p =
      have_item_args ? (const uint16_t*)PyArray_DATA(item_instance_id) : NULL;
  const uint16_t* item_attack_id_p =
      have_item_args ? (const uint16_t*)PyArray_DATA(item_attack_id) : NULL;
  const uint16_t* item_attack_inst_p =
      have_item_args ? (const uint16_t*)PyArray_DATA(item_attack_instance) : NULL;
  uint16_t* out_attack_id_p = (uint16_t*)PyArray_DATA(out_attack_id);
  uint16_t* out_attack_inst_p = (uint16_t*)PyArray_DATA(out_attack_inst);
  uint8_t* out_qi_p = (uint8_t*)PyArray_DATA(out_qi);
  uint16_t* out_mid_p = (uint16_t*)PyArray_DATA(out_mid);
  uint16_t* out_inst_p = (uint16_t*)PyArray_DATA(out_inst);

  uint8_t qi[MSL_MAX_PLAYERS] = {0};
  uint16_t table_mid[MSL_MAX_PLAYERS][10] = {{0}};
  uint16_t table_inst[MSL_MAX_PLAYERS][10] = {{0}};
  uint16_t cur_attack_id[MSL_MAX_PLAYERS];
  uint16_t cur_attack_inst[MSL_MAX_PLAYERS] = {0};
  uint16_t prev_action_id[MSL_MAX_PLAYERS];
  uint16_t prev_state_iid[MSL_MAX_PLAYERS] = {0};
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    cur_attack_id[p] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
    prev_action_id[p] = 0xFFFFu;
  }
  uint16_t stale_attack_counter = 1u;
#define RESET_STALE_TABLE(P)         \
  do {                               \
    qi[(P)] = 0u;                    \
    memset(table_mid[(P)], 0, 20u);  \
    memset(table_inst[(P)], 0, 20u); \
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

  for (npy_intp t = 0; t < n; t++) {
    if (t > 0) {
      for (int p = 0; p < num_players; p++) {
        if (stocks_p[t * w + p] < stocks_p[(t - 1) * w + p]) {
          RESET_STALE_TABLE(p);
          RESET_ATTACK_IDENTITY(p);
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = t * w + p;
      const uint16_t iid = iid_p[pi];
      if (iid == 0u) {
        RESET_ATTACK_IDENTITY(p);
        out_attack_id_p[pi] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
        continue;
      }
      const uint16_t act = action_p[pi];
      if (act != prev_action_id[p]) {
        if (act == ACT_GUARD_OFF && prev_action_id[p] == ACT_GUARD_ON && t > 0 &&
            action_p[(t - 1) * w + p] == ACT_GUARD_ON &&
            anim_p[(t - 1) * w + p] == NO_SUBMOTION_INDEX && frame_p[(t - 1) * w + p] < 0.0f) {
          const uint16_t hidden_guard_inst =
              msl_py_inc_attack_instance_counter(&stale_attack_counter);
          cur_attack_id[p] = attack_id_move_id_from_action(char_p[pi], ACT_GUARD);
          cur_attack_inst[p] = hidden_guard_inst;
        }

        const uint16_t move_id = attack_id_move_id_from_action(char_p[pi], act);
        if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || move_id != cur_attack_id[p]) {
          cur_attack_id[p] = move_id;
          cur_attack_inst[p] = msl_py_inc_attack_instance_counter(&stale_attack_counter);
        }
        prev_action_id[p] = act;
      } else if ((act == ACT_FX_SPECIAL_N_LOOP || act == ACT_FX_SPECIAL_AIR_N_LOOP) &&
                 iid != prev_state_iid[p] && frame_p[pi] == 0.0f) {
        cur_attack_inst[p] = msl_py_inc_attack_instance_counter(&stale_attack_counter);
      }

      if (iid != prev_state_iid[p]) {
        const size_t mi = (size_t)p * 65536u + (size_t)iid;
        if (map_mid[mi] == 0xFFFFu && map_inst[mi] == 0u) {
          map_mid[mi] = cur_attack_id[p];
          map_inst[mi] = cur_attack_inst[p];
        }
        prev_state_iid[p] = iid;
      }
      out_attack_inst_p[pi] = cur_attack_inst[p];
      out_attack_id_p[pi] = cur_attack_id[p];
    }

    if (t > 0) {
      for (int victim = 0; victim < num_players; victim++) {
        const npy_intp vi = t * w + victim;
        const float dp = percent_p[vi] - percent_p[(t - 1) * w + victim];
        if (!(dp > 0.0f)) {
          continue;
        }
        int attacker = -1;
        uint16_t att_move_id = 0xFFFFu;
        uint16_t att_attack_inst = 0u;
        const uint16_t hit_iid = last_hit_iid_p[vi];
        const uint8_t port0 = last_hit_p[vi];
        if (port0 < 4u) {
          attacker = slot_by_port0[port0];
        }
        if (attacker < 0) {
          if (hit_iid != 0u) {
            int match = -1;
            int match_count = 0;
            for (int p = 0; p < num_players; p++) {
              if (iid_p[t * w + p] == hit_iid) {
                match = p;
                match_count++;
              }
            }
            if (match_count == 1) {
              attacker = match;
            }
          }
        }

        uint8_t hit_iid_matches_fighter_row = 0u;
        if (hit_iid != 0u) {
          for (int p = 0; p < num_players; p++) {
            if (iid_p[t * w + p] == hit_iid) {
              if (port0 >= 4u || slot_by_port0[port0] < 0 || slot_by_port0[port0] == p) {
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

        if (have_item_args && hit_iid != 0u && fighter_hit_identity_available == 0u &&
            (att_move_id == 0xFFFFu || att_move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
             att_attack_inst == 0u)) {
          int item_attacker = -1;
          uint16_t item_move_id = 0xFFFFu;
          uint16_t item_attack_inst = 0u;
          int item_match_count = 0;
          // Decomp: item-owned hits update the stale table through
          // plStale_UpdateStaleMovesFromItem(owner, victim), using the item's xD88/xD8C attack
          // identity and its current owner. Reflected lasers can damage on the frame after the
          // item disappears from post-frame, so first consult the previous post-frame item row
          // (the seed_t owner) and then current as a fallback.
          // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078998,ftColl_80077464}
          for (int pass = 0; pass < 2; pass++) {
            const npy_intp item_t = (pass == 0) ? (t - 1) : t;
            if (item_t < 0 || item_t >= n) {
              continue;
            }
            for (npy_intp it = 0; it < item_width; it++) {
              const npy_intp ii = item_t * item_width + it;
              if (item_exists_p[ii] == 0u || item_iid_p[ii] != hit_iid) {
                continue;
              }
              const int owner = (int)item_owner_p[ii];
              if (owner < 0 || owner >= num_players || owner == victim) {
                continue;
              }
              if (port0 < 4u && slot_by_port0[port0] >= 0 && slot_by_port0[port0] != owner) {
                continue;
              }
              item_attacker = owner;
              item_move_id = item_attack_id_p[ii];
              item_attack_inst = item_attack_inst_p[ii];
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
        for (int k = 0; k < 10; k++) {
          if (table_mid[attacker][k] == att_move_id && table_inst[attacker][k] == att_attack_inst) {
            duplicate = 1u;
            break;
          }
        }
        if (!duplicate) {
          int pos = qi[attacker];
          if (pos >= 10) {
            pos = 0;
          }
          table_mid[attacker][pos] = att_move_id;
          table_inst[attacker][pos] = att_attack_inst;
          qi[attacker] = (uint8_t)(pos == 9 ? 0 : pos + 1);
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      out_qi_p[t * w + p] = qi[p];
      for (int k = 0; k < 10; k++) {
        const npy_intp oi = (t * w + p) * 10 + k;
        out_mid_p[oi] = table_mid[p][k];
        out_inst_p[oi] = table_inst[p][k];
      }
    }
  }

#undef RESET_STALE_TABLE
#undef RESET_ATTACK_IDENTITY
  PyMem_Free(map_mid);
  PyMem_Free(map_inst);
  return Py_BuildValue("NNNNN", out_attack_id, out_attack_inst, out_qi, out_mid, out_inst);
}

PyObject* msl_derive_illusion_ghost_pos012_py(PyObject* self, PyObject* args) {
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
  uint8_t prev_valid;
  uint16_t flags;
  int16_t def_frame;
  uint8_t group;
  uint8_t rehit;
  uint8_t element;
  uint8_t _pad0;
  float prev_x;
  float prev_y;
  float prev_z;
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

static inline uint8_t msl_py_hitbox_hitbox_intersects_swept(const MslPyHbPrim* a,
                                                            const MslPyHbPrim* b) {
  if (a == NULL || b == NULL || !a->valid || !b->valid) {
    return 0u;
  }
  const float ax0 = a->prev_valid ? a->prev_x : a->x;
  const float ay0 = a->prev_valid ? a->prev_y : a->y;
  const float az0 = a->prev_valid ? a->prev_z : a->z;
  const float bx0 = b->prev_valid ? b->prev_x : b->x;
  const float by0 = b->prev_valid ? b->prev_y : b->y;
  const float bz0 = b->prev_valid ? b->prev_z : b->z;
  float d2 = 0.0f;
  combat_segment_segment_dist2(ax0, ay0, az0, a->x, a->y, a->z, bx0, by0, bz0, b->x, b->y, b->z,
                               &d2, NULL, NULL);
  const float rr = a->r + b->r;
  return d2 <= (rr * rr) ? 1u : 0u;
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

static inline void msl_py_register_hitbox_contact_seed(
    MslPyHbPrim hitboxes[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES],
    uint16_t hitlist_cd[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS],
    uint16_t hitlist_iid[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS],
    uint16_t hitlist_hb_cd[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS],
    uint16_t hitlist_hb_iid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS],
    uint8_t hitlist_hb_authoritative[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES], int attacker,
    int hit_group, int defender, uint16_t defender_iid, uint8_t rehit) {
  const uint16_t seeded = rehit == 0u ? 0xFFFFu : (uint16_t)rehit;
  const uint8_t group = (uint8_t)(hit_group & 0x7);
  for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
    if (hitboxes[attacker][reg].valid && (hitboxes[attacker][reg].group & 0x7u) == group) {
      hitlist_hb_cd[attacker][reg][defender] = seeded;
      hitlist_hb_iid[attacker][reg][defender] = defender_iid;
      hitlist_hb_authoritative[attacker][reg] = 1u;
    }
  }
  hitlist_cd[attacker][group][defender] = seeded;
  hitlist_iid[attacker][group][defender] = defender_iid;
}

PyObject* msl_derive_combat_hitlist_seed_fields_py(PyObject* self, PyObject* args) {
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
  // Team Attack is assumed on for simulator teams mode, so team identity remains a match-flow /
  // observation lane and is not a combat eligibility filter.
  (void)is_teams;

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
  (void)team_p;
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
  // Throw-swing-hit reconstruction running state: set once the grabbed victim's percent rises during
  // the thrower's active throw-swing hitbox window, held until the throw/thrown pairing ends.
  uint8_t throw_swing_hit[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS] = {{0}};
  uint8_t prev_hb_group[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
  float prev_hb_x[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0.0f}};
  float prev_hb_y[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0.0f}};
  float prev_hb_z[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0.0f}};

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

    uint8_t frame_has_active_hitbox = 0u;
    for (int p = 0; p < num_players && frame_has_active_hitbox == 0u; p++) {
      const npy_intp pi = fi * width + p;
      if (action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      uint16_t frame = (uint16_t)action_frame_p[pi];
      if (anim_frame_p != NULL) {
        float af_f = anim_frame_p[pi];
        if (isfinite(af_f) && af_f >= 0.0f) {
          if (frame_speed_p != NULL && (hitlag_p == NULL || hitlag_p[pi] == 0u)) {
            af_f += frame_speed_p[pi];
          }
          if (af_f < 0.0f) af_f = 0.0f;
          if (af_f > 65535.0f) af_f = 65535.0f;
          frame = (uint16_t)floorf(af_f);
        }
      }
      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(char_p[pi], (uint16_t)anim_p[pi], &events, &event_count) != 0 ||
          events == NULL)
        continue;
      const MslHitboxEvent* active[MSL_MAX_HITBOXES] = {0};
      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        if (ev->frame > frame) continue;
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
        if (active[hb_id] != NULL) {
          frame_has_active_hitbox = 1u;
          break;
        }
      }
    }

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
      if (frame_has_active_hitbox != 0u && hurtcaps_get(cid, &hc, &hc_count) == 0 && hc != NULL) {
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
          hb->group = (uint8_t)((ev->u16_7 >> 8) & 0x7u);
          hb->prev_valid =
              (prev_hb_active[p][hb_id] && prev_hb_group[p][hb_id] == hb->group) ? 1u : 0u;
          hb->prev_x = hb->prev_valid ? prev_hb_x[p][hb_id] : (facing_dir * lz + px);
          hb->prev_y = hb->prev_valid ? prev_hb_y[p][hb_id] : (ly + py);
          hb->prev_z = hb->prev_valid ? prev_hb_z[p][hb_id] : (-facing_dir * lx);
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
          hb->rehit = (uint8_t)(ev->u16_7 & 0xFFu);
          hb->element = (uint8_t)(ev->u16_4 & 0xFFu);
        }
      }

      shield_x[p] = px;
      shield_y[p] = py;
      shield_z[p] = 0.0f;
      shield_r[p] = 0.0f;
      if (frame_has_active_hitbox != 0u && stocks_p[pi] != 0u &&
          msl_py_is_shield_active_action(action_p[pi])) {
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

    if (hitlag_p != NULL) {
      for (int p0 = 0; p0 < num_players; p0++) {
        const npy_intp p0i = fi * width + p0;
        if (stocks_p[p0i] == 0u || !on_ground_p[p0i]) {
          continue;
        }
        for (int p1 = p0 + 1; p1 < num_players; p1++) {
          const npy_intp p1i = fi * width + p1;
          if (stocks_p[p1i] == 0u || !on_ground_p[p1i]) {
            continue;
          }
          if (hitlag_p[p0i] == 0u && hitlag_p[p1i] == 0u) {
            continue;
          }

          for (int hb1 = 0; hb1 < MSL_MAX_HITBOXES; hb1++) {
            MslPyHbPrim* h1 = &hitboxes[p1][hb1];
            if (!h1->valid || (h1->flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
                (h1->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u ||
                h1->element == (uint8_t)MSL_HIT_ELEMENT_INERT || !(h1->damage > 0.0f)) {
              continue;
            }
            for (int hb0 = 0; hb0 < MSL_MAX_HITBOXES; hb0++) {
              MslPyHbPrim* h0 = &hitboxes[p0][hb0];
              if (!h0->valid || (h0->flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
                  (h0->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u ||
                  h0->element == (uint8_t)MSL_HIT_ELEMENT_INERT || !(h0->damage > 0.0f)) {
                continue;
              }
              if (!msl_py_hitbox_hitbox_intersects_swept(h0, h1)) {
                continue;
              }

              const int raw0 = (int)h0->damage;
              const int raw1 = (int)h1->damage;
              const uint8_t p0_side =
                  ((raw0 - common->clank_damage_diff_threshold) < raw1 && hitlag_p[p0i] != 0u) ? 1u
                                                                                               : 0u;
              const uint8_t p1_side =
                  ((raw1 - common->clank_damage_diff_threshold) < raw0 && hitlag_p[p1i] != 0u) ? 1u
                                                                                               : 0u;
              if (p0_side) {
                // Seed-time reconstruction of ftColl_8007699C's type=3 victim insert:
                // active clank hitboxes overlap through lbColl_80007AFC and the replay-visible
                // clank hitlag proves this side branch ran. Materialize the per-HitCapsule
                // victims_1 lane so one-step reseeds into the frozen hitlag tail do not re-clank.
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
                // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
                msl_py_register_hitbox_contact_seed(
                    hitboxes, hitlist_cd, hitlist_iid, hitlist_hb_cd, hitlist_hb_iid,
                    hitlist_hb_authoritative, p0, h0->group, p1, instance_id_p[p1i], h0->rehit);
              }
              if (p1_side) {
                msl_py_register_hitbox_contact_seed(
                    hitboxes, hitlist_cd, hitlist_iid, hitlist_hb_cd, hitlist_hb_iid,
                    hitlist_hb_authoritative, p1, h1->group, p0, instance_id_p[p0i], h1->rehit);
              }
            }
          }
        }
      }
    }

    for (int attacker = 0; attacker < num_players; attacker++) {
      const npy_intp ai = fi * width + attacker;
      if (stocks_p[ai] == 0u) {
        memset(hitlist_hb_cd[attacker], 0, sizeof(hitlist_hb_cd[attacker]));
        memset(hitlist_hb_iid[attacker], 0, sizeof(hitlist_hb_iid[attacker]));
        memset(prev_hb_active[attacker], 0, sizeof(prev_hb_active[attacker]));
        memset(prev_hb_x[attacker], 0, sizeof(prev_hb_x[attacker]));
        memset(prev_hb_y[attacker], 0, sizeof(prev_hb_y[attacker]));
        memset(prev_hb_z[attacker], 0, sizeof(prev_hb_z[attacker]));
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
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (cur_active[hb_id]) {
          prev_hb_x[attacker][hb_id] = hitboxes[attacker][hb_id].x;
          prev_hb_y[attacker][hb_id] = hitboxes[attacker][hb_id].y;
          prev_hb_z[attacker][hb_id] = hitboxes[attacker][hb_id].z;
        } else {
          prev_hb_x[attacker][hb_id] = 0.0f;
          prev_hb_y[attacker][hb_id] = 0.0f;
          prev_hb_z[attacker][hb_id] = 0.0f;
        }
      }

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
              const uint16_t defender_iid = instance_id_p[di];
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = seeded;
                  hitlist_hb_iid[attacker][reg][defender] = defender_iid;
                  hitlist_hb_authoritative[attacker][reg] = 1u;
                }
              }
              hitlist_cd[attacker][hit_group][defender] = seeded;
              hitlist_iid[attacker][hit_group][defender] = defender_iid;
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

    // Throw-swing-hit seed reconstruction (validation/reseed completeness; NOT a runtime change).
    // A throw-swing create_hitbox (e.g. Sheik ThrowLw f31, 5%) hits the grabbed victim once, but the
    // per-frame overlap reconstruction above computes the victim's hurtcaps from their OWN thrown-state
    // pose while the source position is thrower-attachment-driven, so the overlap is missed and the
    // thrower's hitlist is never seeded. One-step reseed inside the still-active hitbox window then
    // re-applies the (staled) hit. Reconstruct the seed from the ground-truth replay-prefix signal: the
    // grabbed victim's percent rose during the thrower's active single-hit (rehit==0) throw-swing
    // hitbox. General across throws/characters; no row-id or character branch.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c (throw-swing create_hitbox vs grabbed victim)
    if (percent_p != NULL) {
      for (int a = 0; a < num_players; a++) {
        const npy_intp a_pi = fi * width + a;
        const uint8_t a_in_throw = (action_p[a_pi] >= (uint16_t)MSL_ACT_THROW_F &&
                                    action_p[a_pi] <= (uint16_t)MSL_ACT_THROW_LW)
                                       ? 1u
                                       : 0u;
        for (int d = 0; d < num_players; d++) {
          if (d == a) {
            continue;
          }
          const npy_intp d_pi = fi * width + d;
          const uint8_t d_thrown = (action_p[d_pi] >= (uint16_t)MSL_ACT_THROWN_F &&
                                    action_p[d_pi] <= (uint16_t)MSL_ACT_THROWN_LW_WOMEN)
                                       ? 1u
                                       : 0u;
          if (!a_in_throw || !d_thrown) {
            throw_swing_hit[a][d] = 0u;
            continue;
          }
          // Pairing: the thrown victim's instance_hit_by MUST be the thrower's current (grab/throw)
          // instance. This is the standard hit-attribution and is the whole safety argument for the
          // reconstruction, so a missing or mismatched pairing is NOT acceptable: clear the latch and
          // skip even while both players are still in Throw/Thrown states. The port-level last_hit_by is
          // not updated by the throw-swing hit and so cannot stand in for it. The latch is held only
          // while the Throw/Thrown pairing remains valid; it (re-)latches on the ground-truth percent
          // edge and keeps seeding on later hold frames (no further rise) while the hitbox stays active.
          if (instance_hit_by_p == NULL || instance_hit_by_p[d_pi] != instance_id_p[a_pi]) {
            throw_swing_hit[a][d] = 0u;
            continue;
          }
          if (fi > 0 && percent_p[d_pi] > percent_p[(fi - 1) * width + d]) {
            throw_swing_hit[a][d] = 1u;
          }
          if (!throw_swing_hit[a][d]) {
            continue;
          }
          // Seed every active single-hit throw-swing hitbox's per-hitbox (authoritative) list as
          // already-hit so a one-step reseed inside the active window does not re-apply the hit. Both
          // same-group swing hitboxes are seeded individually, so the dense group list is not seeded
          // here -- that keeps the seed scoped to the active-hitbox window (the per-hitbox list resets
          // when the hitbox deactivates, while the dense group list would persist past the throw).
          for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
            MslPyHbPrim* hb = &hitboxes[a][reg];
            if (!hb->valid || !(hb->damage > 0.0f) || hb->rehit != 0u) {
              continue;
            }
            hitlist_hb_cd[a][reg][d] = 0xFFFFu;
            hitlist_hb_iid[a][reg][d] = instance_id_p[d_pi];
            hitlist_hb_authoritative[a][reg] = 1u;
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

PyObject* msl_derive_specialhi_rotate_model_seed_lane_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* vel_x_obj = NULL;
  PyObject* vel_y_obj = NULL;
  PyObject* kind_obj = NULL;
  PyObject* x0_obj = NULL;
  PyObject* y0_obj = NULL;
  PyObject* x1_obj = NULL;
  PyObject* y1_obj = NULL;
  int stage_id = 0;
  int act_hi = 0;
  int act_air_hi = 0;
  int act_landing = 0;
  int act_fall = 0;
  int act_bound = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOiOOOOOiiiii", &action_obj, &facing_obj, &pos_x_obj, &pos_y_obj,
                        &vel_x_obj, &vel_y_obj, &stage_id, &kind_obj, &x0_obj, &y0_obj, &x1_obj,
                        &y1_obj, &act_hi, &act_air_hi, &act_landing, &act_fall, &act_bound)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* vel_x =
      require_contiguous_array(vel_x_obj, NPY_FLOAT32, 1, "speed_air_x_self_f32");
  PyArrayObject* vel_y = require_contiguous_array(vel_y_obj, NPY_FLOAT32, 1, "speed_y_self_f32");
  PyArrayObject* kind = require_contiguous_array(kind_obj, NPY_UINT8, 1, "segment_kind");
  PyArrayObject* x0 = require_contiguous_array(x0_obj, NPY_FLOAT32, 1, "segment_x0");
  PyArrayObject* y0 = require_contiguous_array(y0_obj, NPY_FLOAT32, 1, "segment_y0");
  PyArrayObject* x1 = require_contiguous_array(x1_obj, NPY_FLOAT32, 1, "segment_x1");
  PyArrayObject* y1 = require_contiguous_array(y1_obj, NPY_FLOAT32, 1, "segment_y1");
  if (action == NULL || facing == NULL || pos_x == NULL || pos_y == NULL || vel_x == NULL ||
      vel_y == NULL || kind == NULL || x0 == NULL || y0 == NULL || x1 == NULL || y1 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(facing) != n || PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n ||
      PyArray_SIZE(vel_x) != n || PyArray_SIZE(vel_y) != n) {
    PyErr_SetString(PyExc_ValueError, "SpecialHi rotateModel seed inputs must have equal lengths");
    return NULL;
  }
  const npy_intp seg_n = PyArray_SIZE(kind);
  if (PyArray_SIZE(x0) != seg_n || PyArray_SIZE(y0) != seg_n || PyArray_SIZE(x1) != seg_n ||
      PyArray_SIZE(y1) != seg_n) {
    PyErr_SetString(PyExc_ValueError,
                    "SpecialHi rotateModel segment arrays must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* valid = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL || valid == NULL) {
    Py_XDECREF(out);
    Py_XDECREF(valid);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* f = (const uint8_t*)PyArray_DATA(facing);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const float* vx = (const float*)PyArray_DATA(vel_x);
  const float* vy = (const float*)PyArray_DATA(vel_y);
  const uint8_t* sk = (const uint8_t*)PyArray_DATA(kind);
  const float* sx0 = (const float*)PyArray_DATA(x0);
  const float* sy0 = (const float*)PyArray_DATA(y0);
  const float* sx1 = (const float*)PyArray_DATA(x1);
  const float* sy1 = (const float*)PyArray_DATA(y1);
  float* o = (float*)PyArray_DATA(out);
  uint8_t* v = (uint8_t*)PyArray_DATA(valid);
  const uint16_t hi = (uint16_t)((uint32_t)act_hi & 0xFFFFu);
  const uint16_t air_hi = (uint16_t)((uint32_t)act_air_hi & 0xFFFFu);
  const uint16_t landing = (uint16_t)((uint32_t)act_landing & 0xFFFFu);
  const uint16_t fall = (uint16_t)((uint32_t)act_fall & 0xFFFFu);
  const uint16_t bound = (uint16_t)((uint32_t)act_bound & 0xFFFFu);
  float cur = 0.0f;
  bool cur_valid = false;
  uint16_t prev_action = 0xFFFFu;
  uint8_t prev_facing = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    const bool special = ai == hi || ai == air_hi || ai == landing || ai == fall || ai == bound;
    if (!special) {
      cur_valid = false;
      prev_action = ai;
      prev_facing = f[i];
      continue;
    }
    bool collision_refresh = false;
    if (stage_id == 32 && (ai == hi || ai == air_hi)) {
      const float x = px[i];
      const float y = py[i];
      if (isfinite(x) && isfinite(y)) {
        for (npy_intp sidx = 0; sidx < seg_n; sidx++) {
          if (!(sk[sidx] == 2u || sk[sidx] == 3u)) continue;
          const float wall_x =
              msl_py_segment_x_at_y(y, sx0[sidx], sy0[sidx], sx1[sidx], sy1[sidx], false);
          if (!isfinite(wall_x)) continue;
          const float delta = sk[sidx] == 3u ? (wall_x - x) : (x - wall_x);
          if (delta >= 0.0f && delta <= 6.0f) {
            collision_refresh = true;
            break;
          }
        }
      }
    }
    const bool has_vel =
        isfinite(vx[i]) && isfinite(vy[i]) && (fabsf(vx[i]) > 0.0f || fabsf(vy[i]) > 0.0f);
    const bool prev_special = prev_action == hi || prev_action == air_hi ||
                              prev_action == landing || prev_action == fall || prev_action == bound;
    const bool recompute = !cur_valid || !prev_special || f[i] != prev_facing || collision_refresh;
    if (recompute && has_vel) {
      const float fx = f[i] != 0u ? 1.0f : -1.0f;
      cur = atan2f(vy[i], vx[i] * fx);
      cur_valid = true;
    }
    if (cur_valid) {
      o[i] = cur;
      v[i] = 1u;
    }
    prev_action = ai;
    prev_facing = f[i];
  }
  return Py_BuildValue("NN", out, valid);
}

PyObject* msl_trim_stale_hitlist_seed_bridge_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* hitlist_cd_obj = NULL;
  PyObject* hitlist_iid_obj = NULL;
  PyObject* hitlist_hb_valid_obj = NULL;
  PyObject* hitlist_hb_cd_obj = NULL;
  PyObject* hitlist_hb_iid_obj = NULL;
  PyObject* post_instance_id_obj = NULL;
  PyObject* post_instance_hit_by_obj = NULL;
  PyObject* post_last_hit_by_obj = NULL;
  PyObject* post_hitlag_obj = NULL;
  PyObject* post_hitstun_obj = NULL;
  PyObject* post_action_obj = NULL;
  PyObject* landing_fallspecial_allow_obj = NULL;
  int num_players = 0;
  int act_attack_11 = 0;
  int act_attack_lw4 = 0;
  int act_damage_fly_top = 0;
  int act_landing_fall_special = 0;
  int act_guard_on = 0;
  int act_guard = 0;
  int act_guard_set_off = 0;
  int act_guard_reflect = 0;
  int act_guard_off = 0;
  if (!PyArg_ParseTuple(
          args, "OOOOOOOOOOOOiiiiiiiiii", &hitlist_cd_obj, &hitlist_iid_obj, &hitlist_hb_valid_obj,
          &hitlist_hb_cd_obj, &hitlist_hb_iid_obj, &post_instance_id_obj, &post_instance_hit_by_obj,
          &post_last_hit_by_obj, &post_hitlag_obj, &post_hitstun_obj, &post_action_obj,
          &landing_fallspecial_allow_obj, &num_players, &act_attack_11, &act_attack_lw4,
          &act_damage_fly_top, &act_landing_fall_special, &act_guard_on, &act_guard,
          &act_guard_set_off, &act_guard_reflect, &act_guard_off)) {
    return NULL;
  }
  PyArrayObject* hitlist_cd = require_contiguous_array(hitlist_cd_obj, NPY_UINT16, 4, "hitlist_cd");
  PyArrayObject* hitlist_iid =
      require_contiguous_array(hitlist_iid_obj, NPY_UINT16, 4, "hitlist_iid");
  PyArrayObject* hitlist_hb_valid =
      require_contiguous_array(hitlist_hb_valid_obj, NPY_UINT8, 3, "hitlist_hb_valid");
  PyArrayObject* hitlist_hb_cd =
      require_contiguous_array(hitlist_hb_cd_obj, NPY_UINT16, 4, "hitlist_hb_cd");
  PyArrayObject* hitlist_hb_iid =
      require_contiguous_array(hitlist_hb_iid_obj, NPY_UINT16, 4, "hitlist_hb_iid");
  PyArrayObject* post_instance_id =
      require_contiguous_array(post_instance_id_obj, NPY_UINT16, 2, "post_instance_id");
  PyArrayObject* post_instance_hit_by =
      require_contiguous_array(post_instance_hit_by_obj, NPY_UINT16, 2, "post_instance_hit_by");
  PyArrayObject* post_last_hit_by =
      require_contiguous_array(post_last_hit_by_obj, NPY_UINT8, 2, "post_last_hit_by");
  PyArrayObject* post_hitlag =
      require_contiguous_array(post_hitlag_obj, NPY_UINT16, 2, "post_hitlag");
  PyArrayObject* post_hitstun =
      require_contiguous_array(post_hitstun_obj, NPY_UINT16, 2, "post_hitstun");
  PyArrayObject* post_action =
      require_contiguous_array(post_action_obj, NPY_UINT16, 2, "post_action");
  PyArrayObject* landing_fallspecial_allow = require_contiguous_array(
      landing_fallspecial_allow_obj, NPY_UINT8, 2, "landing_fallspecial_allow_interrupt");
  if (hitlist_cd == NULL || hitlist_iid == NULL || hitlist_hb_valid == NULL ||
      hitlist_hb_cd == NULL || hitlist_hb_iid == NULL || post_instance_id == NULL ||
      post_instance_hit_by == NULL || post_last_hit_by == NULL || post_hitlag == NULL ||
      post_hitstun == NULL || post_action == NULL || landing_fallspecial_allow == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(hitlist_cd) != 4 || PyArray_NDIM(hitlist_iid) != 4 ||
      PyArray_NDIM(hitlist_hb_valid) != 3 || PyArray_NDIM(hitlist_hb_cd) != 4 ||
      PyArray_NDIM(hitlist_hb_iid) != 4) {
    PyErr_SetString(PyExc_ValueError, "hitlist arrays have invalid rank");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(hitlist_cd, 0);
  const npy_intp width = PyArray_DIM(hitlist_cd, 1);
  const npy_intp coarse_hb = PyArray_DIM(hitlist_cd, 2);
  const npy_intp defenders = PyArray_DIM(hitlist_cd, 3);
  const npy_intp hb_count = PyArray_DIM(hitlist_hb_valid, 2);
  if (PyArray_DIM(hitlist_iid, 0) != n || PyArray_DIM(hitlist_iid, 1) != width ||
      PyArray_DIM(hitlist_iid, 2) != coarse_hb || PyArray_DIM(hitlist_iid, 3) != defenders ||
      PyArray_DIM(hitlist_hb_valid, 0) != n || PyArray_DIM(hitlist_hb_valid, 1) != width ||
      PyArray_DIM(hitlist_hb_cd, 0) != n || PyArray_DIM(hitlist_hb_cd, 1) != width ||
      PyArray_DIM(hitlist_hb_cd, 2) != hb_count || PyArray_DIM(hitlist_hb_cd, 3) != defenders ||
      PyArray_DIM(hitlist_hb_iid, 0) != n || PyArray_DIM(hitlist_hb_iid, 1) != width ||
      PyArray_DIM(hitlist_hb_iid, 2) != hb_count || PyArray_DIM(hitlist_hb_iid, 3) != defenders ||
      require_exact_2d_shape(post_instance_id, n, width, "post_instance_id") < 0 ||
      require_exact_2d_shape(post_instance_hit_by, n, width, "post_instance_hit_by") < 0 ||
      require_exact_2d_shape(post_last_hit_by, n, width, "post_last_hit_by") < 0 ||
      require_exact_2d_shape(post_hitlag, n, width, "post_hitlag") < 0 ||
      require_exact_2d_shape(post_hitstun, n, width, "post_hitstun") < 0 ||
      require_exact_2d_shape(post_action, n, width, "post_action") < 0 ||
      require_exact_2d_shape(landing_fallspecial_allow, n, width,
                             "landing_fallspecial_allow_interrupt") < 0) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players ||
      defenders < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for hitlist dimensions");
    return NULL;
  }
  const int players = num_players;
  uint16_t* cd = (uint16_t*)PyArray_DATA(hitlist_cd);
  uint16_t* iid = (uint16_t*)PyArray_DATA(hitlist_iid);
  const uint8_t* hb_valid = (const uint8_t*)PyArray_DATA(hitlist_hb_valid);
  uint16_t* hb_cd = (uint16_t*)PyArray_DATA(hitlist_hb_cd);
  uint16_t* hb_iid = (uint16_t*)PyArray_DATA(hitlist_hb_iid);
  const uint16_t* inst = (const uint16_t*)PyArray_DATA(post_instance_id);
  const uint16_t* inst_by = (const uint16_t*)PyArray_DATA(post_instance_hit_by);
  const uint8_t* last_by = (const uint8_t*)PyArray_DATA(post_last_hit_by);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(post_hitlag);
  const uint16_t* hitstun = (const uint16_t*)PyArray_DATA(post_hitstun);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(post_action);
  const uint8_t* lfs_allow = (const uint8_t*)PyArray_DATA(landing_fallspecial_allow);
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int attacker = 0; attacker < players; attacker++) {
      const uint16_t attacker_iid = inst[(fi * width) + attacker];
      for (int defender = 0; defender < players; defender++) {
        const npy_intp def_idx = (fi * width) + defender;
        if (inst_by[def_idx] == attacker_iid) continue;
        const uint16_t defender_action = action[def_idx];
        const uint8_t last_hit_by_owner = last_by[def_idx];
        const uint16_t owner_iid = inst_by[def_idx];
        bool owner_matches = last_hit_by_owner == (uint8_t)attacker;
        if (!owner_matches) {
          bool owner_iid_matches_live = false;
          for (int p = 0; p < players; p++) {
            if (inst[(fi * width) + p] == owner_iid) {
              owner_iid_matches_live = true;
              break;
            }
          }
          owner_matches = players == 2 && attacker != defender &&
                          defender_action > (uint16_t)act_attack_lw4 &&
                          last_hit_by_owner >= (uint8_t)players && !owner_iid_matches_live;
        }
        int hitlag_pre = (int)hitlag[def_idx];
        if (hitlag_pre > 0) hitlag_pre--;
        int hitstun_pre = (int)hitstun[def_idx];
        if (hitstun_pre > 0) hitstun_pre--;
        const bool landing_fallspecial_neutral_trim =
            hitlag_pre == 0 && hitstun_pre == 0 &&
            defender_action == (uint16_t)act_landing_fall_special && lfs_allow[def_idx] == 0u;
        if (!owner_matches && !landing_fallspecial_neutral_trim) continue;
        const bool guard_family = defender_action == (uint16_t)act_guard_on ||
                                  defender_action == (uint16_t)act_guard ||
                                  defender_action == (uint16_t)act_guard_set_off ||
                                  defender_action == (uint16_t)act_guard_reflect ||
                                  defender_action == (uint16_t)act_guard_off;
        // LandingFallSpecial can only reach the common guard-input admission path when the hidden
        // mv.co.landing.allow_interrupt bit is true. Treat non-interrupt LandingFallSpecial like a
        // neutral non-guard stale-latch trim target rather than preserving dense victims_1 solely
        // from the visible action id.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}
        const bool landing_fallspecial_guard_admission =
            defender_action == (uint16_t)act_landing_fall_special && lfs_allow[def_idx] != 0u;
        const bool neutral_non_guard = hitlag_pre == 0 && hitstun_pre == 0 && !guard_family &&
                                       !landing_fallspecial_guard_admission;
        const uint16_t attacker_action = action[(fi * width) + attacker];
        const bool damage_state_bridge = hitlag_pre == 0 &&
                                         defender_action == (uint16_t)act_damage_fly_top &&
                                         attacker_action >= (uint16_t)act_attack_11 &&
                                         attacker_action <= (uint16_t)act_attack_lw4;
        if (!(neutral_non_guard || damage_state_bridge)) continue;
        for (npy_intp hb = 0; hb < coarse_hb; hb++) {
          const npy_intp idx = (((fi * width + attacker) * coarse_hb + hb) * defenders) + defender;
          if (cd[idx] == 0xFFFFu) {
            cd[idx] = 0u;
            iid[idx] = 0u;
          }
        }
        for (npy_intp hb = 0; hb < hb_count; hb++) {
          const npy_intp idx = (((fi * width + attacker) * hb_count + hb) * defenders) + defender;
          const npy_intp valid_idx = ((fi * width + attacker) * hb_count) + hb;
          if (hb_cd[idx] == 0xFFFFu && hb_valid[valid_idx] == 0u) {
            hb_cd[idx] = 0u;
            hb_iid[idx] = 0u;
          }
        }
      }
    }
  }
  Py_RETURN_NONE;
}

static float msl_py_stale_multiplier_from_queue(uint8_t queue_index, const uint16_t* move_ids,
                                                npy_intp stride, uint16_t move_id,
                                                const float* weights, npy_intp weight_count) {
  if (move_id == 0xFFFFu || move_id == 1u) return 1.0f;
  const int qi = queue_index < 10u ? (int)queue_index : 0;
  int pos = qi != 0 ? qi - 1 : 9;
  float mult = 1.0f;
  for (int i = 0; i < 9; i++) {
    if (move_ids[(npy_intp)pos] == move_id && i < weight_count) {
      mult -= weights[i];
    }
    pos = pos != 0 ? pos - 1 : 9;
  }
  (void)stride;
  return mult;
}

static int msl_py_env_dmg_from_float(float dmg) {
  if (dmg == 0.0f) return 0;
  const int i = (int)dmg;
  return i != 0 ? i : 1;
}

PyObject* msl_derive_attacker_shield_ground_kb_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* ground_friction_mul_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* guard_damage_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_damage_lut_obj = NULL;
  PyObject* char_friction_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  int act_guard_reflect = 0;
  double shield_kb_mul = 0.0;
  double shield_kb_base = 0.0;
  double shield_kb_friction_mul = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOiiiddd", &action_obj, &attack_id_obj,
                        &anim_frame_obj, &animation_index_obj, &on_ground_obj, &hitlag_obj,
                        &pos_x_obj, &char_obj, &ground_friction_mul_obj, &lightshield_obj,
                        &guard_damage_obj, &stale_queue_obj, &stale_move_id_obj,
                        &active_damage_lut_obj, &char_friction_lut_obj, &stale_weights_obj,
                        &num_players, &act_guard_set_off, &act_guard_reflect, &shield_kb_mul,
                        &shield_kb_base, &shield_kb_friction_mul)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* ground_friction_mul =
      require_contiguous_array(ground_friction_mul_obj, NPY_FLOAT32, 2, "ground_friction_mul");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* guard_damage =
      require_contiguous_array(guard_damage_obj, NPY_UINT8, 2, "guard_setoff_hitlag_damage_min");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_damage_lut =
      require_contiguous_array(active_damage_lut_obj, NPY_UINT16, 3, "active_damage_lut");
  PyArrayObject* char_friction_lut =
      require_contiguous_array(char_friction_lut_obj, NPY_FLOAT32, 1, "char_friction_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  if (action == NULL || attack_id == NULL || anim_frame == NULL || animation_index == NULL ||
      on_ground == NULL || hitlag == NULL || pos_x == NULL || chr == NULL ||
      ground_friction_mul == NULL || lightshield == NULL || guard_damage == NULL ||
      stale_queue == NULL || stale_move_id == NULL || active_damage_lut == NULL ||
      char_friction_lut == NULL || stale_weights == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(attack_id, n, width, "attack_id_u16") < 0 ||
      require_exact_2d_shape(anim_frame, n, width, "anim_frame_f32") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(on_ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x_f32") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(ground_friction_mul, n, width, "ground_friction_mul") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      require_exact_2d_shape(guard_damage, n, width, "guard_setoff_hitlag_damage_min") < 0 ||
      require_exact_2d_shape(stale_queue, n, width, "stale_queue_index") < 0 ||
      PyArray_NDIM(stale_move_id) != 3 || PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != width || PyArray_DIM(stale_move_id, 2) < 10 ||
      PyArray_NDIM(active_damage_lut) != 3 || PyArray_DIM(active_damage_lut, 0) < 256 ||
      PyArray_SIZE(char_friction_lut) < 256 || PyArray_SIZE(stale_weights) < 9) {
    PyErr_SetString(PyExc_ValueError, "attacker shield ground kb inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for player width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const float* anim_data = (const float*)PyArray_DATA(anim_frame);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const uint8_t* ground_data = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const float* pos_x_data = (const float*)PyArray_DATA(pos_x);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const float* ground_mul_data = (const float*)PyArray_DATA(ground_friction_mul);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint8_t* guard_damage_data = (const uint8_t*)PyArray_DATA(guard_damage);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_damage = (const uint16_t*)PyArray_DATA(active_damage_lut);
  const float* char_friction = (const float*)PyArray_DATA(char_friction_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  float* o = (float*)PyArray_DATA(out);
  const npy_intp active_anim_cap = PyArray_DIM(active_damage_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_damage_lut, 2);
  for (int attacker = 0; attacker < players; attacker++) {
    float kb = 0.0f;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp idx = (i * width) + attacker;
      if (ground_data[idx] == 0u) {
        kb = 0.0f;
        o[(i * 4) + attacker] = kb;
        continue;
      }
      bool has_onset = false;
      float onset_kb = 0.0f;
      if (hitlag_data[idx] > 0u) {
        for (int defender = 0; defender < players; defender++) {
          if (defender == attacker) continue;
          const npy_intp def_idx = (i * width) + defender;
          if (hitlag_data[def_idx] == 0u) continue;
          const uint16_t defender_action = action_data[def_idx];
          if (defender_action != (uint16_t)act_guard_set_off &&
              defender_action != (uint16_t)act_guard_reflect) {
            continue;
          }
          const uint16_t prev_attacker_hitlag =
              i > 0 ? hitlag_data[((i - 1) * width) + attacker] : 0u;
          const uint16_t prev_defender_hitlag =
              i > 0 ? hitlag_data[((i - 1) * width) + defender] : 0u;
          const uint8_t prev_defender_dmg =
              i > 0 ? guard_damage_data[((i - 1) * width) + defender] : 0u;
          if (prev_attacker_hitlag != 0u && prev_defender_hitlag != 0u && prev_defender_dmg > 0u) {
            continue;
          }
          int int_dmg = 0;
          const uint8_t cid = char_data[idx];
          const uint32_t anim_idx = anim_idx_data[idx];
          int anim_frame_i = (int)floorf(anim_data[idx]);
          if (anim_frame_i < 0) anim_frame_i = 0;
          if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)anim_frame_i < active_frame_cap) {
            int_dmg = (int)
                active_damage[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                              anim_frame_i];
          }
          if (int_dmg > 0) {
            const uint16_t move_id = attack_data[idx];
            const uint16_t* queue =
                &stale_move_data[((i * width + attacker) * PyArray_DIM(stale_move_id, 2))];
            const float stale_mult = msl_py_stale_multiplier_from_queue(
                stale_queue_data[idx], queue, PyArray_DIM(stale_move_id, 2), move_id, weights,
                PyArray_SIZE(stale_weights));
            int_dmg = msl_py_env_dmg_from_float((float)int_dmg * stale_mult);
          }
          if (int_dmg <= 0) int_dmg = (int)guard_damage_data[def_idx];
          if (int_dmg <= 0) continue;
          const float eval_kb =
              light_data[def_idx] * (float)int_dmg * (float)shield_kb_mul + (float)shield_kb_base;
          onset_kb = pos_x_data[def_idx] > pos_x_data[idx] ? -eval_kb : eval_kb;
          has_onset = true;
          break;
        }
      }
      if (has_onset) kb = onset_kb;
      o[(i * 4) + attacker] = kb;
      if (hitlag_data[idx] > 1u || kb == 0.0f) continue;
      const float friction =
          ground_mul_data[idx] * char_friction[char_data[idx]] * (float)shield_kb_friction_mul;
      if (friction <= 0.0f || fabsf(friction) >= fabsf(kb)) {
        kb = 0.0f;
      } else if (kb < 0.0f) {
        kb += friction;
      } else {
        kb -= friction;
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guardsetoff_frame_speed_overrides_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* state_age_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* shield_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_damage_lut_obj = NULL;
  PyObject* end_frame_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  double shield_hit_mul = 0.0;
  double shield_hit_base = 0.0;
  double shield_hit_ls_min = 0.0;
  double shield_hit_ls_max = 0.0;
  double shield_stun_mul = 0.0;
  double shield_stun_base = 0.0;
  double shield_stun_ls_min = 0.0;
  double shield_stun_ls_max = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOiidddddddd", &action_obj, &hitlag_obj, &state_age_obj,
                        &animation_index_obj, &char_obj, &flags_obj, &shield_obj, &lightshield_obj,
                        &attack_id_obj, &stale_queue_obj, &stale_move_id_obj,
                        &active_damage_lut_obj, &end_frame_lut_obj, &stale_weights_obj,
                        &num_players, &act_guard_set_off, &shield_hit_mul, &shield_hit_base,
                        &shield_hit_ls_min, &shield_hit_ls_max, &shield_stun_mul, &shield_stun_base,
                        &shield_stun_ls_min, &shield_stun_ls_max)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* state_age = require_contiguous_array(state_age_obj, NPY_INT16, 2, "state_age_i16");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 3, "state_flags_u8");
  PyArrayObject* shield = require_contiguous_array(shield_obj, NPY_FLOAT32, 2, "shield_f32");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_damage_lut =
      require_contiguous_array(active_damage_lut_obj, NPY_UINT16, 3, "active_damage_lut");
  PyArrayObject* end_frame_lut =
      require_contiguous_array(end_frame_lut_obj, NPY_FLOAT32, 2, "end_frame_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  if (action == NULL || hitlag == NULL || state_age == NULL || animation_index == NULL ||
      chr == NULL || flags == NULL || shield == NULL || lightshield == NULL || attack_id == NULL ||
      stale_queue == NULL || stale_move_id == NULL || active_damage_lut == NULL ||
      end_frame_lut == NULL || stale_weights == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(state_age, n, width, "state_age_i16") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(shield, n, width, "shield_f32") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      PyArray_NDIM(attack_id) != 2 || PyArray_DIM(attack_id, 0) != n ||
      PyArray_NDIM(stale_queue) != 2 || PyArray_DIM(stale_queue, 0) != n ||
      PyArray_NDIM(flags) != 3 || PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) != width ||
      PyArray_DIM(flags, 2) < 4 || PyArray_NDIM(stale_move_id) != 3 ||
      PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_queue, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_move_id, 2) < 10 || PyArray_NDIM(active_damage_lut) != 3 ||
      PyArray_DIM(active_damage_lut, 0) < 256 || PyArray_NDIM(end_frame_lut) != 2 ||
      PyArray_DIM(end_frame_lut, 0) < 256 || PyArray_DIM(end_frame_lut, 1) <= 40 ||
      PyArray_SIZE(stale_weights) < 9) {
    PyErr_SetString(PyExc_ValueError, "GuardSetOff frame-speed inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for player width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const int16_t* age_data = (const int16_t*)PyArray_DATA(state_age);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* shield_data = (const float*)PyArray_DATA(shield);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_damage = (const uint16_t*)PyArray_DATA(active_damage_lut);
  const float* end_frames = (const float*)PyArray_DATA(end_frame_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  float* o = (float*)PyArray_DATA(out);
  const npy_intp active_anim_cap = PyArray_DIM(active_damage_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_damage_lut, 2);
  const npy_intp end_width = PyArray_DIM(end_frame_lut, 1);
  const npy_intp flags_depth = PyArray_DIM(flags, 2);
  const npy_intp hist_width = PyArray_DIM(attack_id, 1);
  if (hist_width < players) {
    PyErr_SetString(PyExc_ValueError, "GuardSetOff history arrays are narrower than num_players");
    Py_DECREF(out);
    return NULL;
  }
  for (int defender = 0; defender < players; defender++) {
    float carry_rate = 0.0f;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp def_idx = (i * width) + defender;
      if (action_data[def_idx] != (uint16_t)act_guard_set_off) {
        carry_rate = 0.0f;
        continue;
      }
      const uint16_t cur_hl = hitlag_data[def_idx];
      const uint16_t prev_action = i > 0 ? action_data[((i - 1) * width) + defender] : 0xFFFFu;
      const uint16_t prev_hl = i > 0 ? hitlag_data[((i - 1) * width) + defender] : 0u;
      const int prev_af = i > 0 ? (int)age_data[((i - 1) * width) + defender] : 0;
      const int cur_af = (int)age_data[def_idx];
      const bool segment_entry = i == 0 || prev_action != (uint16_t)act_guard_set_off ||
                                 cur_hl > prev_hl || cur_af < prev_af;
      if (segment_entry && cur_hl > 0u) {
        int best = 0;
        for (int attacker = 0; attacker < players; attacker++) {
          if (attacker == defender) continue;
          const npy_intp atk_idx = (i * width) + attacker;
          if (hitlag_data[atk_idx] == 0u) continue;
          const uint8_t cid = char_data[atk_idx];
          const uint32_t anim_idx = anim_idx_data[atk_idx];
          int frame = (int)age_data[atk_idx];
          if (frame < 0) frame = 0;
          int dmg = 0;
          if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)frame < active_frame_cap) {
            dmg = (int)
                active_damage[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                              frame];
          }
          if (dmg <= 0) continue;
          const npy_intp hist_idx = (i * hist_width) + attacker;
          const uint16_t move_id = attack_data[hist_idx];
          const uint16_t* queue =
              &stale_move_data[((i * hist_width + attacker) * PyArray_DIM(stale_move_id, 2))];
          const float stale_mult = msl_py_stale_multiplier_from_queue(
              stale_queue_data[hist_idx], queue, PyArray_DIM(stale_move_id, 2), move_id, weights,
              PyArray_SIZE(stale_weights));
          dmg = msl_py_env_dmg_from_float((float)dmg * stale_mult);
          if (dmg > best) best = dmg;
        }
        if (best > 0) {
          float hidden_light = light_data[def_idx];
          bool inferred_light_ok = hidden_light > 0.0f;
          if (!inferred_light_ok && i > 0 && shield_hit_mul > 0.0) {
            const float shield_drop =
                shield_data[((i - 1) * width) + defender] - shield_data[def_idx];
            const float hit_den = (float)shield_hit_mul * (float)best;
            if (shield_drop > 0.0f && hit_den > 0.0f && shield_hit_ls_max != shield_hit_ls_min) {
              const float hit_light_term =
                  1.0f - ((shield_drop - (float)shield_hit_base) / hit_den);
              if (isfinite(hit_light_term)) {
                const float inferred = (hit_light_term - (float)shield_hit_ls_min) /
                                       ((float)shield_hit_ls_max - (float)shield_hit_ls_min);
                if (inferred >= -0.001f && inferred <= 1.001f) {
                  hidden_light = fminf(fmaxf(inferred, 0.0f), 1.0f);
                  inferred_light_ok = true;
                }
              }
            }
          } else {
            hidden_light = fminf(fmaxf(hidden_light, 0.0f), 1.0f);
          }
          const bool powershield_active = (flags_data[(def_idx * flags_depth) + 3] & 0x20u) != 0u;
          if (!(powershield_active || inferred_light_ok)) {
            carry_rate = 0.0f;
            continue;
          }
          const float stun_light_term =
              hidden_light * ((float)shield_stun_ls_max - (float)shield_stun_ls_min) +
              (float)shield_stun_ls_min;
          const float stun_frames =
              (float)shield_stun_mul * ((float)best * (1.0f - stun_light_term)) +
              (float)shield_stun_base;
          const uint8_t def_cid = char_data[def_idx];
          const float end_frame = end_frames[((npy_intp)def_cid * end_width) + 40];
          if (end_frame > 0.0f && stun_frames > 0.0f) {
            carry_rate = ((float)((float)end_frame + 0.1f)) / stun_frames;
          }
        }
      }
      if (cur_hl > 0u && carry_rate > 0.0f) {
        o[(i * 4) + defender] = carry_rate;
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_marth_counter_hitlag_floor_active_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* state_flags_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &char_obj, &action_obj, &state_flags_obj)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action =
      require_contiguous_array_readonly(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* flags =
      require_contiguous_array_readonly(state_flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (chr == NULL || action == NULL || flags == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  if (PyArray_NDIM(chr) != 1 || PyArray_NDIM(action) != 1 || PyArray_NDIM(flags) != 2 ||
      PyArray_DIM(chr, 0) != n || PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 3) {
    PyErr_SetString(PyExc_ValueError,
                    "Marth Counter hitlag-floor inputs must be char/action length N and "
                    "state_flags shape [N, >=3]");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) {
    return NULL;
  }

  const uint8_t* charp = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* actionp = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* flagsp = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp flags_w = PyArray_DIM(flags, 1);
  uint8_t* outp = (uint8_t*)PyArray_DATA(out);

  enum {
    CHAR_MARTH = 18,
    ACT_MARTH_COUNTER_GROUND = 369,
    ACT_MARTH_COUNTER_AIR = 371,
  };
  // Marth Counter ShieldDesc liveness is visible as fp+0x221B_b0 (state_flags[2] 0x80).
  // ftMs_SpecialLw_Anim / ftMs_SpecialAirLw_Anim create the descriptor with MarsAttributes::x60
  // shield_unk0/1; ftMs_SpecialLw_80138D38 / 80138DD0 recreate it across 369<->371 swaps
  // without restoring shield_unk0/1, so the hitlag-floor provenance clears on those swaps.
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c
  uint8_t active_floor = 0u;
  bool prev_live = false;
  uint16_t prev_action = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t action_id = actionp[i];
    const bool counter_action =
        action_id == ACT_MARTH_COUNTER_GROUND || action_id == ACT_MARTH_COUNTER_AIR;
    const bool live =
        charp[i] == CHAR_MARTH && counter_action && ((flagsp[(i * flags_w) + 2] & 0x80u) != 0u);
    if (!live) {
      active_floor = 0u;
      prev_live = false;
      prev_action = action_id;
      continue;
    }
    const bool swapped =
        prev_live &&
        ((prev_action == ACT_MARTH_COUNTER_GROUND && action_id == ACT_MARTH_COUNTER_AIR) ||
         (prev_action == ACT_MARTH_COUNTER_AIR && action_id == ACT_MARTH_COUNTER_GROUND));
    if (swapped) {
      active_floor = 0u;
    } else if (!prev_live) {
      active_floor = 1u;
    }
    outp[i] = active_floor;
    prev_live = true;
    prev_action = action_id;
  }
  return (PyObject*)out;
}

static inline uint8_t msl_py_lut_u8(const uint8_t* lut, npy_intp lut_n, uint16_t key) {
  return (npy_intp)key < lut_n ? lut[key] : 0u;
}

static int msl_py_invert_hitlag_min_damage(int hitlag_frames, float slope, float base) {
  if (hitlag_frames <= 0) return 0;
  for (int dmg = 1; dmg < 0xFF; dmg++) {
    if ((int)(((float)dmg * slope) + base) >= hitlag_frames) return dmg;
  }
  return 0xFF;
}

static int msl_py_infer_shield_damage_taken(float shield_now, float shield_next, float light,
                                            bool guard_now, float shield_hit_mul,
                                            float shield_hit_base, float shield_hit_ls_min,
                                            float shield_hit_ls_max, float hold_drain_mul,
                                            float hold_drain_base, float hold_drain_max) {
  if (!(shield_hit_mul > 0.0f)) return 0;
  float shield_drop = shield_now - shield_next;
  if (!(shield_drop > 0.0f)) return 0;
  if (light < 0.0f) light = 0.0f;
  if (light > 1.0f) light = 1.0f;
  if (guard_now) {
    const float drain_factor = light * (hold_drain_max - hold_drain_base) + hold_drain_base;
    shield_drop -= hold_drain_mul * drain_factor;
  }
  const float light_term = light * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min;
  const float denom = shield_hit_mul * (1.0f - light_term);
  if (!(denom > 0.0f)) return 0;
  const float raw = (shield_drop - shield_hit_base) / denom;
  if (!isfinite(raw)) return 0;
  int dmg = (int)nearbyintf(raw);
  if (dmg <= 0) return 0;
  if (dmg > 0xFF) dmg = 0xFF;
  const float predicted = shield_hit_mul * ((float)dmg * (1.0f - light_term)) + shield_hit_base;
  if (fabsf(predicted - shield_drop) > 0.35f) return 0;
  return dmg;
}

PyObject* msl_derive_shield_contact_seed_bridge_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* shield_contact_obj = NULL;
  PyObject* hitlist_valid_obj = NULL;
  PyObject* hitlist_cd_obj = NULL;
  PyObject* hitlist_iid_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_id_obj = NULL;
  PyObject* shield_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* state_age_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_shield_hit_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  PyObject* guard_lut_obj = NULL;
  PyObject* attack_lut_obj = NULL;
  PyObject* same_frame_lut_obj = NULL;
  PyObject* same_frame_special_lut_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  double hitlag_dmg_mul = 0.0;
  double hitlag_base = 0.0;
  double shield_hit_mul = 0.0;
  double shield_hit_base = 0.0;
  double shield_hit_ls_min = 0.0;
  double shield_hit_ls_max = 0.0;
  double hold_drain_mul = 0.0;
  double hold_drain_base = 0.0;
  double hold_drain_max = 0.0;
  if (!PyArg_ParseTuple(
          args, "OOOOOOOOOOOOOOOOOOOOOiiddddddddd", &shield_contact_obj, &hitlist_valid_obj,
          &hitlist_cd_obj, &hitlist_iid_obj, &action_obj, &hitlag_obj, &instance_id_obj,
          &shield_obj, &lightshield_obj, &animation_index_obj, &state_age_obj, &char_obj,
          &attack_id_obj, &stale_queue_obj, &stale_move_id_obj, &active_shield_hit_lut_obj,
          &stale_weights_obj, &guard_lut_obj, &attack_lut_obj, &same_frame_lut_obj,
          &same_frame_special_lut_obj, &num_players, &act_guard_set_off, &hitlag_dmg_mul,
          &hitlag_base, &shield_hit_mul, &shield_hit_base, &shield_hit_ls_min, &shield_hit_ls_max,
          &hold_drain_mul, &hold_drain_base, &hold_drain_max)) {
    return NULL;
  }
  PyArrayObject* shield_contact =
      require_contiguous_array(shield_contact_obj, NPY_UINT8, 4, "shield_contact_hb_kind");
  PyArrayObject* hitlist_valid =
      require_contiguous_array(hitlist_valid_obj, NPY_UINT8, 3, "hitlist_hb_valid");
  PyArrayObject* hitlist_cd =
      require_contiguous_array(hitlist_cd_obj, NPY_UINT16, 4, "hitlist_hb_cd");
  PyArrayObject* hitlist_iid =
      require_contiguous_array(hitlist_iid_obj, NPY_UINT16, 4, "hitlist_hb_iid");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* instance_id =
      require_contiguous_array(instance_id_obj, NPY_UINT16, 2, "instance_id_u16");
  PyArrayObject* shield = require_contiguous_array(shield_obj, NPY_FLOAT32, 2, "shield_f32");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* state_age = require_contiguous_array(state_age_obj, NPY_INT16, 2, "state_age_i16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_shield_hit_lut =
      require_contiguous_array(active_shield_hit_lut_obj, NPY_UINT16, 3, "active_shield_hit_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  PyArrayObject* guard_lut = require_contiguous_array(guard_lut_obj, NPY_UINT8, 1, "guard_lut");
  PyArrayObject* attack_lut = require_contiguous_array(attack_lut_obj, NPY_UINT8, 1, "attack_lut");
  PyArrayObject* same_frame_lut =
      require_contiguous_array(same_frame_lut_obj, NPY_UINT8, 1, "same_frame_lut");
  PyArrayObject* same_frame_special_lut =
      require_contiguous_array(same_frame_special_lut_obj, NPY_UINT8, 1, "same_frame_special_lut");
  if (shield_contact == NULL || hitlist_valid == NULL || hitlist_cd == NULL ||
      hitlist_iid == NULL || action == NULL || hitlag == NULL || instance_id == NULL ||
      shield == NULL || lightshield == NULL || animation_index == NULL || state_age == NULL ||
      chr == NULL || attack_id == NULL || stale_queue == NULL || stale_move_id == NULL ||
      active_shield_hit_lut == NULL || stale_weights == NULL || guard_lut == NULL ||
      attack_lut == NULL || same_frame_lut == NULL || same_frame_special_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(instance_id, n, width, "instance_id_u16") < 0 ||
      require_exact_2d_shape(shield, n, width, "shield_f32") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(state_age, n, width, "state_age_i16") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      PyArray_NDIM(shield_contact) != 4 || PyArray_DIM(shield_contact, 0) != n ||
      PyArray_DIM(shield_contact, 1) < width || PyArray_DIM(shield_contact, 3) < width ||
      PyArray_NDIM(hitlist_valid) != 3 || PyArray_DIM(hitlist_valid, 0) != n ||
      PyArray_DIM(hitlist_valid, 1) < width ||
      PyArray_DIM(hitlist_valid, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_NDIM(hitlist_cd) != 4 || PyArray_DIM(hitlist_cd, 0) != n ||
      PyArray_DIM(hitlist_cd, 1) < width ||
      PyArray_DIM(hitlist_cd, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_DIM(hitlist_cd, 3) < width || PyArray_NDIM(hitlist_iid) != 4 ||
      PyArray_DIM(hitlist_iid, 0) != n || PyArray_DIM(hitlist_iid, 1) < width ||
      PyArray_DIM(hitlist_iid, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_DIM(hitlist_iid, 3) < width || PyArray_NDIM(attack_id) != 2 ||
      PyArray_DIM(attack_id, 0) != n || PyArray_NDIM(stale_queue) != 2 ||
      PyArray_DIM(stale_queue, 0) != n || PyArray_NDIM(stale_move_id) != 3 ||
      PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_queue, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_move_id, 2) < 10 || PyArray_NDIM(active_shield_hit_lut) != 3 ||
      PyArray_DIM(active_shield_hit_lut, 0) < 256 || PyArray_SIZE(stale_weights) < 9 ||
      PyArray_SIZE(guard_lut) < 65536 || PyArray_SIZE(attack_lut) < 65536 ||
      PyArray_SIZE(same_frame_lut) < 65536 || PyArray_SIZE(same_frame_special_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "shield contact seed bridge inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for player width");
    return NULL;
  }
  const int players = num_players;
  if (PyArray_DIM(attack_id, 1) < players) {
    PyErr_SetString(PyExc_ValueError, "shield contact history arrays are narrower than players");
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed for shield contact seed bridge");
    return NULL;
  }
  npy_intp out_dims[2] = {n, 4};
  PyArrayObject* out_hit_damage = (PyArrayObject*)PyArray_ZEROS(2, out_dims, NPY_UINT8, 0);
  PyArrayObject* out_shield_taken = (PyArrayObject*)PyArray_ZEROS(2, out_dims, NPY_UINT8, 0);
  if (out_hit_damage == NULL || out_shield_taken == NULL) {
    Py_XDECREF(out_hit_damage);
    Py_XDECREF(out_shield_taken);
    return NULL;
  }
  uint8_t* contact = (uint8_t*)PyArray_DATA(shield_contact);
  uint8_t* valid = (uint8_t*)PyArray_DATA(hitlist_valid);
  uint16_t* cd = (uint16_t*)PyArray_DATA(hitlist_cd);
  uint16_t* iid_out = (uint16_t*)PyArray_DATA(hitlist_iid);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* iid = (const uint16_t*)PyArray_DATA(instance_id);
  const float* shield_data = (const float*)PyArray_DATA(shield);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const int16_t* age_data = (const int16_t*)PyArray_DATA(state_age);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_lut = (const uint16_t*)PyArray_DATA(active_shield_hit_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  const uint8_t* guard = (const uint8_t*)PyArray_DATA(guard_lut);
  const uint8_t* attack = (const uint8_t*)PyArray_DATA(attack_lut);
  const uint8_t* same_frame = (const uint8_t*)PyArray_DATA(same_frame_lut);
  const uint8_t* same_frame_special = (const uint8_t*)PyArray_DATA(same_frame_special_lut);
  uint8_t* hit_damage = (uint8_t*)PyArray_DATA(out_hit_damage);
  uint8_t* shield_taken = (uint8_t*)PyArray_DATA(out_shield_taken);
  const npy_intp hb_count = PyArray_DIM(shield_contact, 2);
  const npy_intp contact_w = PyArray_DIM(shield_contact, 1);
  const npy_intp contact_dw = PyArray_DIM(shield_contact, 3);
  const npy_intp hv_w = PyArray_DIM(hitlist_valid, 1);
  const npy_intp hcd_w = PyArray_DIM(hitlist_cd, 1);
  const npy_intp hcd_dw = PyArray_DIM(hitlist_cd, 3);
  const npy_intp hii_w = PyArray_DIM(hitlist_iid, 1);
  const npy_intp hii_dw = PyArray_DIM(hitlist_iid, 3);
  const npy_intp hist_width = PyArray_DIM(attack_id, 1);
  const npy_intp stale_depth = PyArray_DIM(stale_move_id, 2);
  const npy_intp active_anim_cap = PyArray_DIM(active_shield_hit_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_shield_hit_lut, 2);
  const npy_intp guard_n = PyArray_SIZE(guard_lut);
  const npy_intp attack_n = PyArray_SIZE(attack_lut);
  const npy_intp same_frame_n = PyArray_SIZE(same_frame_lut);
  const npy_intp same_frame_special_n = PyArray_SIZE(same_frame_special_lut);

#define MSL_CONTACT_IDX(frame, attacker, hb, defender) \
  ((((frame) * contact_w + (attacker)) * hb_count + (hb)) * contact_dw + (defender))
#define MSL_VALID_IDX(frame, attacker, hb) (((frame) * hv_w + (attacker)) * hb_count + (hb))
#define MSL_CD_IDX(frame, attacker, hb, defender) \
  ((((frame) * hcd_w + (attacker)) * hb_count + (hb)) * hcd_dw + (defender))
#define MSL_IID_IDX(frame, attacker, hb, defender) \
  ((((frame) * hii_w + (attacker)) * hb_count + (hb)) * hii_dw + (defender))

  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      const npy_intp def_idx = i * width + defender;
      const npy_intp def_next_idx = (i + 1) * width + defender;
      const bool defender_guard_now = msl_py_lut_u8(guard, guard_n, action_data[def_idx]) != 0u;
      const bool defender_guard_next =
          msl_py_lut_u8(guard, guard_n, action_data[def_next_idx]) != 0u;
      if (!(defender_guard_now || defender_guard_next)) continue;
      if (hitlag_data[def_idx] != 0u) continue;
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        const npy_intp atk_idx = i * width + attacker;
        const npy_intp atk_next_idx = (i + 1) * width + attacker;
        const bool owner = msl_py_lut_u8(attack, attack_n, action_data[atk_idx]) != 0u ||
                           msl_py_lut_u8(same_frame, same_frame_n, action_data[atk_next_idx]) != 0u;
        if (!owner) continue;
        if (hitlag_data[atk_idx] != 0u) continue;
        if (action_data[def_next_idx] == (uint16_t)act_guard_set_off &&
            hitlag_data[def_next_idx] > 0u && hitlag_data[atk_next_idx] > 0u) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] = 2u;
          }
          const int hitlag_int_dmg = msl_py_invert_hitlag_min_damage(
              (int)hitlag_data[def_next_idx], (float)hitlag_dmg_mul, (float)hitlag_base);
          int active_int_dmg = 0;
          for (int other = 0; other < players; other++) {
            if (other == defender || hitlag_data[((i + 1) * width) + other] == 0u) continue;
            const npy_intp other_idx = ((i + 1) * width) + other;
            const uint8_t cid = char_data[other_idx];
            const uint32_t anim_idx = anim_idx_data[other_idx];
            int frame = (int)age_data[other_idx];
            if (frame < 0) frame = 0;
            int int_dmg = 0;
            if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)frame < active_frame_cap) {
              int_dmg = (int)
                  active_lut[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                             frame];
            }
            if (int_dmg <= 0) continue;
            const npy_intp hist_idx = ((i + 1) * hist_width) + other;
            const uint16_t move_id = attack_data[hist_idx];
            const uint16_t* queue =
                &stale_move_data[(((i + 1) * hist_width) + other) * stale_depth];
            const float stale_mult =
                msl_py_stale_multiplier_from_queue(stale_queue_data[hist_idx], queue, stale_depth,
                                                   move_id, weights, PyArray_SIZE(stale_weights));
            int_dmg = msl_py_env_dmg_from_float((float)int_dmg * stale_mult);
            if (int_dmg > active_int_dmg) active_int_dmg = int_dmg;
          }
          const bool use_active_upper = msl_py_lut_u8(same_frame_special, same_frame_special_n,
                                                      action_data[atk_next_idx]) != 0u;
          if (active_int_dmg > 0 && hitlag_int_dmg > 0) {
            active_int_dmg = use_active_upper && active_int_dmg > hitlag_int_dmg ? active_int_dmg
                                                                                 : hitlag_int_dmg;
          } else if (active_int_dmg <= 0) {
            active_int_dmg = hitlag_int_dmg;
          }
          const npy_intp out_idx = i * 4 + defender;
          if (active_int_dmg > hit_damage[out_idx]) hit_damage[out_idx] = (uint8_t)active_int_dmg;
          const int taken = msl_py_infer_shield_damage_taken(
              shield_data[def_idx], shield_data[def_next_idx], light_data[def_idx],
              defender_guard_now, (float)shield_hit_mul, (float)shield_hit_base,
              (float)shield_hit_ls_min, (float)shield_hit_ls_max, (float)hold_drain_mul,
              (float)hold_drain_base, (float)hold_drain_max);
          if (taken > hit_damage[out_idx] && taken > shield_taken[out_idx]) {
            shield_taken[out_idx] = (uint8_t)taken;
          }
        } else if ((hitlag_data[def_next_idx] == 0u && hitlag_data[atk_next_idx] == 0u) ||
                   (hitlag_data[def_next_idx] > 0u && hitlag_data[atk_next_idx] > 0u &&
                    action_data[def_next_idx] != (uint16_t)act_guard_set_off)) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] = 1u;
          }
        }
      }
    }
  }

  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        bool has_proven = false;
        for (npy_intp hb = 0; hb < hb_count; hb++) {
          if (contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] == 2u) {
            has_proven = true;
            break;
          }
        }
        if (i + 1 < n && action_data[(i * width) + defender] == (uint16_t)act_guard_set_off &&
            action_data[(i * width) + attacker] == (uint16_t)MSL_ACT_ATTACK_AIR_N &&
            hitlag_data[(i * width) + attacker] == 0u &&
            hitlag_data[(i * width) + defender] == 0u) {
          const npy_intp release_j = i + 1;
          const uint16_t attacker_action = action_data[(release_j * width) + attacker];
          if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N &&
              msl_py_lut_u8(guard, guard_n, action_data[(release_j * width) + defender]) != 0u &&
              hitlag_data[(release_j * width) + attacker] == 0u &&
              hitlag_data[(release_j * width) + defender] == 0u) {
            int frame = (int)age_data[(release_j * width) + attacker];
            if (frame < 0) frame = 0;
            if (move_tables_attackair_same_group_payload_preserves_hitcapsule(
                    char_data[(release_j * width) + attacker], attacker_action, (float)frame) !=
                0u) {
              const uint16_t cur_defender_iid = iid[(release_j * width) + defender];
              for (npy_intp hb = 0; hb < hb_count; hb++) {
                if (contact[MSL_CONTACT_IDX(release_j, attacker, hb, defender)] != 1u) {
                  continue;
                }
                // GuardSetOff release can carry an accepted same-group AttackAirN HitCapsule
                // victim list into the first Guard row even when replay no longer exposes hitlag.
                // Keep this on the same MSLFTSC1 create lifetime and the replay-proven ShieldDesc
                // contact marker; do not synthesize it for arbitrary Guard rows.
                // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80078C70}
                // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
                // data/scripts/<char>.bin (MSLFTSC1)::ftCo_SM_AttackAirN create_hitbox/clear_hitboxes
                valid[MSL_VALID_IDX(release_j, attacker, hb)] = 1u;
                cd[MSL_CD_IDX(release_j, attacker, hb, defender)] = 0xFFFFu;
                iid_out[MSL_IID_IDX(release_j, attacker, hb, defender)] = cur_defender_iid;
              }
            }
          }
        }
        if (!has_proven) continue;
        if (hitlag_data[i * width + attacker] != 0u || hitlag_data[i * width + defender] != 0u) {
          continue;
        }
        if (action_data[((i + 1) * width) + defender] != (uint16_t)act_guard_set_off) continue;
        if (hitlag_data[((i + 1) * width) + attacker] == 0u ||
            hitlag_data[((i + 1) * width) + defender] == 0u) {
          continue;
        }
        const uint16_t defender_iid = iid[((i + 1) * width) + defender];
        const uint16_t attacker_action = action_data[((i + 1) * width) + attacker];
        const uint16_t defender_action = action_data[((i + 1) * width) + defender];
        npy_intp j = i + 1;
        while (j < n && action_data[(j * width) + attacker] == attacker_action &&
               action_data[(j * width) + defender] == defender_action &&
               hitlag_data[(j * width) + attacker] > 0u &&
               hitlag_data[(j * width) + defender] > 0u &&
               iid[(j * width) + defender] == defender_iid) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            if (contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] != 2u) continue;
            valid[MSL_VALID_IDX(j, attacker, hb)] = 1u;
            cd[MSL_CD_IDX(j, attacker, hb, defender)] = 0xFFFFu;
            iid_out[MSL_IID_IDX(j, attacker, hb, defender)] = defender_iid;
          }
          j++;
        }
        if (attacker_action != (uint16_t)MSL_ACT_ATTACK_AIR_N) {
          continue;
        }
        while (j < n && action_data[(j * width) + attacker] == attacker_action &&
               msl_py_lut_u8(guard, guard_n, action_data[(j * width) + defender]) != 0u &&
               hitlag_data[(j * width) + attacker] == 0u &&
               hitlag_data[(j * width) + defender] == 0u) {
          int frame = (int)age_data[(j * width) + attacker];
          if (frame < 0) frame = 0;
          if (move_tables_attackair_same_group_payload_preserves_hitcapsule(
                  char_data[(j * width) + attacker], attacker_action, (float)frame) == 0u) {
            break;
          }
          bool carried_any = false;
          const uint16_t cur_defender_iid = iid[(j * width) + defender];
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            if (contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] != 2u ||
                contact[MSL_CONTACT_IDX(j, attacker, hb, defender)] != 1u) {
              continue;
            }
            // Same-group AttackAirN refresh preserves the source HitCapsule.victims_1 list only
            // inside the same MSLFTSC1 create_hitbox lifetime. A later replay-proven ShieldDesc
            // miss is not proof that the concrete HitCapsule victim list was empty, but a script
            // clear/recreate is. Rebind the seed-lane iid to the current visible fighter instance
            // because the simulator uses instance_id as a proxy for vanilla's stable victim
            // pointer through GuardSetOff -> Guard motion entries.
            // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80078C70}
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
            // data/scripts/<char>.bin (MSLFTSC1)::ftCo_SM_AttackAirN create_hitbox/clear_hitboxes
            valid[MSL_VALID_IDX(j, attacker, hb)] = 1u;
            cd[MSL_CD_IDX(j, attacker, hb, defender)] = 0xFFFFu;
            iid_out[MSL_IID_IDX(j, attacker, hb, defender)] = cur_defender_iid;
            carried_any = true;
          }
          if (!carried_any) {
            break;
          }
          j++;
        }
      }
    }
  }

#undef MSL_CONTACT_IDX
#undef MSL_VALID_IDX
#undef MSL_CD_IDX
#undef MSL_IID_IDX

  return Py_BuildValue("NN", out_hit_damage, out_shield_taken);
}

PyObject* msl_derive_rebound_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* seed_speed_obj = NULL;
  PyObject* ref_speed_obj = NULL;
  PyObject* seed_frame_speed_obj = NULL;
  PyObject* numerator_lut_obj = NULL;
  int num_players = 0;
  int act_rebound_stop = 0;
  int act_rebound = 0;
  double x0_mul = 0.0;
  double x0_base = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOiiidd", &action_obj, &hitlag_obj, &ground_obj, &char_obj,
                        &seed_speed_obj, &ref_speed_obj, &seed_frame_speed_obj, &numerator_lut_obj,
                        &num_players, &act_rebound_stop, &act_rebound, &x0_mul, &x0_base)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* seed_speed =
      require_contiguous_array(seed_speed_obj, NPY_FLOAT32, 2, "seed_speed_ground_x");
  PyArrayObject* ref_speed =
      require_contiguous_array(ref_speed_obj, NPY_FLOAT32, 2, "ref_speed_ground_x");
  PyArrayObject* seed_frame_speed =
      require_contiguous_array(seed_frame_speed_obj, NPY_FLOAT32, 2, "seed_frame_speed");
  PyArrayObject* numerator_lut =
      require_contiguous_array(numerator_lut_obj, NPY_FLOAT32, 1, "rebound_numerator_lut");
  if (action == NULL || hitlag == NULL || ground == NULL || chr == NULL || seed_speed == NULL ||
      ref_speed == NULL || seed_frame_speed == NULL || numerator_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 || PyArray_NDIM(seed_speed) != 2 ||
      PyArray_NDIM(ref_speed) != 2 || PyArray_NDIM(seed_frame_speed) != 2 ||
      PyArray_DIM(seed_speed, 0) != n - 1 || PyArray_DIM(ref_speed, 0) != n - 1 ||
      PyArray_DIM(seed_frame_speed, 0) != n - 1 || PyArray_DIM(seed_speed, 1) != width ||
      PyArray_DIM(ref_speed, 1) != width || PyArray_DIM(seed_frame_speed, 1) != width ||
      PyArray_SIZE(numerator_lut) < 256) {
    PyErr_SetString(PyExc_ValueError, "rebound seed lane inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for player width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_accel = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_rate = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out_accel == NULL || out_rate == NULL) {
    Py_XDECREF(out_accel);
    Py_XDECREF(out_rate);
    return NULL;
  }
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* ground_data = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const float* seed_speed_data = (const float*)PyArray_DATA(seed_speed);
  const float* ref_speed_data = (const float*)PyArray_DATA(ref_speed);
  const float* frame_speed_data = (const float*)PyArray_DATA(seed_frame_speed);
  const float* numerator_data = (const float*)PyArray_DATA(numerator_lut);
  float* accel = (float*)PyArray_DATA(out_accel);
  float* rate_out = (float*)PyArray_DATA(out_rate);
  for (int p = 0; p < players; p++) {
    for (npy_intp i = 0; i < n - 1; i++) {
      const npy_intp idx = (i * width) + p;
      const npy_intp next_idx = ((i + 1) * width) + p;
      if (action_data[idx] != (uint16_t)act_rebound_stop || hitlag_data[idx] == 0u ||
          action_data[next_idx] != (uint16_t)act_rebound || hitlag_data[next_idx] != 0u ||
          ground_data[idx] == 0u || ground_data[next_idx] == 0u) {
        continue;
      }
      const float pending_xe8 = ref_speed_data[idx] - seed_speed_data[idx];
      if (!isfinite(pending_xe8) || fabsf(pending_xe8) <= 1.0e-6f || fabsf(pending_xe8) > 2.0f) {
        continue;
      }
      float pending_rate = 0.0f;
      if (i + 2 < n - 1 && action_data[((i + 2) * width) + p] == (uint16_t)act_rebound) {
        const float later_rate = frame_speed_data[((i + 2) * width) + p];
        if (isfinite(later_rate) && later_rate > 0.0f && later_rate < 20.0f) {
          pending_rate = later_rate;
        }
      }
      if (pending_rate == 0.0f && x0_mul > 0.0) {
        const float rebound_x191c = (fabsf(pending_xe8) - (float)x0_base) / (float)x0_mul;
        const float numerator = numerator_data[char_data[idx]];
        if (rebound_x191c > 0.0f && numerator > 0.0f) {
          const float rate = (numerator + 0.1f) / rebound_x191c;
          if (isfinite(rate) && rate > 0.0f && rate < 20.0f) pending_rate = rate;
        }
      }
      npy_intp j = i;
      while (j >= 0 && action_data[(j * width) + p] == (uint16_t)act_rebound_stop &&
             hitlag_data[(j * width) + p] > 0u && ground_data[(j * width) + p] != 0u) {
        accel[(j * 4) + p] = pending_xe8;
        if (pending_rate > 0.0f) rate_out[(j * 4) + p] = pending_rate;
        j--;
      }
      if (pending_rate > 0.0f) rate_out[((i + 1) * 4) + p] = pending_rate;
    }
  }
  return Py_BuildValue("NN", out_accel, out_rate);
}

PyObject* msl_derive_source_clear_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  PyObject* x9_obj = NULL;
  int init_frames = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOi", &action_obj, &char_obj, &ground_obj, &flags_obj, &src_obj,
                        &x9_obj, &init_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* x9 = require_contiguous_array(x9_obj, NPY_UINT8, 2, "x9_b1_lut");
  if (action == NULL || chr == NULL || ground == NULL || flags == NULL || src == NULL ||
      x9 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(ground) != n || PyArray_SIZE(src) != n ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5 || PyArray_DIM(x9, 0) < 256) {
    PyErr_SetString(PyExc_ValueError, "source clear timer inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp action_cap = PyArray_DIM(x9, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_phase = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_timer == NULL || out_phase == NULL) {
    Py_XDECREF(out_timer);
    Py_XDECREF(out_phase);
    return NULL;
  }
  if (init_frames < 0) init_frames = 0;
  if (init_frames > 255) init_frames = 255;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  const uint8_t* x9p = (const uint8_t*)PyArray_DATA(x9);
  uint8_t* timer_o = (uint8_t*)PyArray_DATA(out_timer);
  uint8_t* phase_o = (uint8_t*)PyArray_DATA(out_phase);
  int timer = -1;
  uint8_t edge_pending = 0u;
  uint8_t phase_active = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur_a = a[i];
    const uint8_t cur_src = source[i];
    if (i > 0 && source[i - 1] == 6u && cur_src != 6u) edge_pending = 1u;
    if (i > 0 && cur_a != a[i - 1]) {
      uint8_t x9_b1 = 0u;
      if ((npy_intp)cur_a < action_cap) x9_b1 = x9p[((npy_intp)c[i] * action_cap) + cur_a];
      if (g[i] != 0u && x9_b1 != 0u && timer < 0 && cur_src != 6u) {
        timer = init_frames;
        phase_active = edge_pending ? 1u : 0u;
      }
    }
    if (cur_src == 6u) {
      timer = -1;
      edge_pending = 0u;
      phase_active = 0u;
    }
    const bool x221f_b3 = (sf[(i * sf_w) + 4] & 0x10u) != 0u;
    if (!x221f_b3 && timer >= 0) timer--;
    if (timer >= 0) {
      timer_o[i] = (uint8_t)(timer + 1 > 255 ? 255 : timer + 1);
      phase_o[i] = phase_active ? 1u : 0u;
    } else {
      timer_o[i] = 0u;
      phase_o[i] = 0u;
      phase_active = 0u;
    }
  }
  return Py_BuildValue("NN", out_timer, out_phase);
}

PyObject* msl_derive_source_clear_grounded_damage_clear_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* combo_obj = NULL;
  PyObject* timer_obj = NULL;
  PyObject* phase_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOO", &action_obj, &frame_obj, &ground_obj, &hitlag_obj,
                        &hitstun_obj, &combo_obj, &timer_obj, &phase_obj, &flags_obj, &src_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* combo = require_contiguous_array(combo_obj, NPY_UINT8, 1, "combo_count_u8");
  PyArrayObject* timer = require_contiguous_array(timer_obj, NPY_UINT8, 1, "timer_u8");
  PyArrayObject* phase = require_contiguous_array(phase_obj, NPY_UINT8, 1, "owner_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  if (action == NULL || frame == NULL || ground == NULL || hitlag == NULL || hitstun == NULL ||
      combo == NULL || timer == NULL || phase == NULL || flags == NULL || src == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(ground) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(hitstun) != n || PyArray_SIZE(combo) != n || PyArray_SIZE(timer) != n ||
      PyArray_SIZE(phase) != n || PyArray_SIZE(src) != n || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_grounded_damage_clear_phase inputs have incompatible shapes");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* combo_p = (const uint8_t*)PyArray_DATA(combo);
  const uint8_t* timer_p = (const uint8_t*)PyArray_DATA(timer);
  const uint8_t* phase_p = (const uint8_t*)PyArray_DATA(phase);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 1; i < n; i++) {
    if (source[i] >= 6u || timer_p[i] == 0u || phase_p[i] == 0u || hl[i] != 0u || hs[i] != 0u ||
        g[i] == 0u || (sf[(i * sf_w) + 4] & 0x10u) != 0u) {
      continue;
    }
    if (a[i - 1] == 0x0010u && a[i] == 0x000Eu && af[i] == 0 && combo_p[i] == 0u &&
        timer_p[i - 1] == (uint8_t)(timer_p[i] + 1u)) {
      o[i] = 1u;
      continue;
    }
    if (a[i - 1] == 0x000Fu && a[i] == 0x0014u && af[i] == 1 && timer_p[i] == 1u &&
        timer_p[i - 1] == 2u) {
      o[i] = 1u;
    }
  }
  return (PyObject*)out;
}

static bool msl_source_clear_terminal_action_allowed(uint16_t action) {
  switch (action) {
    case 0x000Eu:  // Wait
    case 0x00B3u:  // Guard
    case 0x00B7u:  // DownBoundU
    case 0x00B8u:  // DownWaitU
    case 0x00BAu:  // DownStandU
    case 0x00BBu:  // DownAttackU
    case 0x00BCu:  // DownForwardU
    case 0x00BDu:  // DownBackU
    case 0x00BFu:  // DownBoundD
    case 0x00C0u:  // DownWaitD
    case 0x00C2u:  // DownStandD
    case 0x00C3u:  // DownAttackD
    case 0x00C4u:  // DownForwardD
    case 0x00C5u:  // DownBackD
    case 0x00C7u:  // Passive
    case 0x00C8u:  // PassiveStandF
    case 0x00C9u:  // PassiveStandB
    case 0x00E9u:  // EscapeF
    case 0x015Eu:  // Fox/Falco SpecialAirSStart
    case 0x0004u:  // DeadUpStar
    case 0x0041u:  // AttackAirN
    case 0x0045u:  // AttackAirLw
    case 0x00ECu:  // EscapeAir
    case 0x0012u:  // Turn
    case 0x0018u:  // KneeBend
    case 0x0019u:  // JumpF
    case 0x001Au:  // JumpB
    case 0x0027u:  // Squat
    case 0x0038u:  // AttackHi3
      return true;
    default:
      return false;
  }
}

static bool msl_source_clear_terminal_followup_action(uint16_t action) {
  return action == 0x0041u || action == 0x0045u || action == 0x00ECu;
}

static bool msl_source_clear_terminal_continuation_action(uint16_t action) {
  return action == 0x0012u || action == 0x0018u || action == 0x0019u || action == 0x001Au ||
         action == 0x0027u || action == 0x0038u;
}

PyObject* msl_derive_source_clear_terminal_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* combo_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* timer_obj = NULL;
  PyObject* owner_phase_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  PyObject* cmd0_on_obj = NULL;
  PyObject* cmd0_off_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOO", &char_obj, &action_obj, &frame_obj, &hitlag_obj,
                        &hitstun_obj, &combo_obj, &last_attack_obj, &timer_obj, &owner_phase_obj,
                        &flags_obj, &src_obj, &cmd0_on_obj, &cmd0_off_obj)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* combo = require_contiguous_array(combo_obj, NPY_UINT8, 1, "combo_count_u8");
  PyArrayObject* last_attack =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 1, "last_attack_landed_u8");
  PyArrayObject* timer = require_contiguous_array(timer_obj, NPY_UINT8, 1, "timer_u8");
  PyArrayObject* owner_phase =
      require_contiguous_array(owner_phase_obj, NPY_UINT8, 1, "owner_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* cmd0_on = require_contiguous_array(cmd0_on_obj, NPY_INT16, 2, "cmd0_on_lut");
  PyArrayObject* cmd0_off = require_contiguous_array(cmd0_off_obj, NPY_INT16, 2, "cmd0_off_lut");
  if (chr == NULL || action == NULL || frame == NULL || hitlag == NULL || hitstun == NULL ||
      combo == NULL || last_attack == NULL || timer == NULL || owner_phase == NULL ||
      flags == NULL || src == NULL || cmd0_on == NULL || cmd0_off == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(hitstun) != n || PyArray_SIZE(combo) != n || PyArray_SIZE(last_attack) != n ||
      PyArray_SIZE(timer) != n || PyArray_SIZE(owner_phase) != n || PyArray_SIZE(src) != n ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5 || PyArray_DIM(cmd0_on, 0) < 256 ||
      PyArray_DIM(cmd0_off, 0) < 256 || PyArray_DIM(cmd0_on, 1) != PyArray_DIM(cmd0_off, 1)) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_terminal_phase inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp action_cap = PyArray_DIM(cmd0_on, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;

  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* combo_p = (const uint8_t*)PyArray_DATA(combo);
  const uint8_t* last_attack_p = (const uint8_t*)PyArray_DATA(last_attack);
  const uint8_t* timer_p = (const uint8_t*)PyArray_DATA(timer);
  const uint8_t* phase_p = (const uint8_t*)PyArray_DATA(owner_phase);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  const int16_t* on_lut = (const int16_t*)PyArray_DATA(cmd0_on);
  const int16_t* off_lut = (const int16_t*)PyArray_DATA(cmd0_off);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);

  for (npy_intp i = 1; i < n; i++) {
    if (timer_p[i] != 1u) continue;
    const uint16_t act = a[i];
    if (!msl_source_clear_terminal_action_allowed(act)) continue;
    if (msl_source_clear_terminal_continuation_action(act) && phase_p[i] == 0u) continue;
    const int cur_af = (int)af[i];
    if (act == 0x0019u) {
      if (cur_af < 10 || cur_af > 20) continue;
    } else if (act == 0x001Au) {
      if (cur_af < 30) continue;
    } else if (act == 0x0018u) {
      if (cur_af != 1) continue;
      if ((sf[(i * sf_w) + 3] & 0x60u) == 0u) continue;
    }
    const uint8_t owner = source[i];
    if (owner >= 6u) continue;
    if (hl[i] != 0u || hs[i] != 0u) continue;
    if (combo_p[i] == 0u || last_attack_p[i] == 0u) {
      if (!(act == 0x015Eu || act == 0x0045u || act == 0x0018u || act == 0x0019u ||
            act == 0x001Au)) {
        continue;
      }
      if ((act == 0x0018u || act == 0x0019u || act == 0x001Au) && last_attack_p[i] == 0u) {
        continue;
      }
    }
    if ((sf[(i * sf_w) + 4] & 0x10u) != 0u) continue;
    if (act != 0x00B3u && !msl_source_clear_terminal_continuation_action(act) &&
        (sf[(i * sf_w) + 1] != 0u || sf[(i * sf_w) + 2] != 0u)) {
      continue;
    }
    if (timer_p[i - 1] != 2u) continue;
    if (source[i - 1] != owner) continue;
    const uint16_t prev_act = a[i - 1];
    const int prev_af = (int)af[i - 1];
    const bool same_action_progress = (act == prev_act && cur_af == prev_af + 1);
    const bool guard_hold_progress =
        (act == 0x00B3u && prev_act == 0x00B3u && cur_af == -1 && prev_af == -1);
    const bool continuation_entry_progress =
        (msl_source_clear_terminal_continuation_action(act) && cur_af == 1 && prev_af >= 0);
    if (!(same_action_progress || guard_hold_progress || continuation_entry_progress)) continue;
    if (msl_source_clear_terminal_followup_action(act) || act == 0x0038u) {
      const uint8_t cid = c[i];
      int cmd0_on = -1;
      int cmd0_off = -1;
      if ((npy_intp)act < action_cap) {
        cmd0_on = (int)on_lut[((npy_intp)cid * action_cap) + act];
        cmd0_off = (int)off_lut[((npy_intp)cid * action_cap) + act];
      }
      if (act == 0x0041u) {
        if (cmd0_on < 0 || cur_af >= cmd0_on) continue;
      } else if (act == 0x00ECu) {
        if (cmd0_on < 0 || cur_af >= cmd0_on) continue;
      } else if (act == 0x0045u) {
        if (cmd0_off < 0 || cur_af >= cmd0_off) continue;
        if (combo_p[i] != 0u) continue;
      } else if (act == 0x0038u) {
        if (cmd0_off < 0 || cur_af >= cmd0_off) continue;
      }
    }
    o[i] = 1u;
  }
  return (PyObject*)out;
}

static float msl_py_randf_after_pre_gate(uint32_t seed_in, int stream_offset_steps,
                                         int consume_count) {
  uint32_t seed = seed_in;
  const int steps = stream_offset_steps + consume_count + 1;
  for (int i = 0; i < steps; i++) {
    seed = seed * 214013u + 2531011u;
  }
  return (float)((seed >> 16) & 0xFFFFu) * (1.0f / 65536.0f);
}

static int msl_py_local_slot_from_source_port(const uint8_t* source_port0, npy_intp width,
                                              npy_intp row, int players, int source_port0_raw) {
  for (int p = 0; p < players; p++) {
    if ((int)source_port0[(row * width) + p] == source_port0_raw) return p;
  }
  return -1;
}

static int msl_py_f26_source_port_for_player(const uint8_t* last_hit_by,
                                             const uint8_t* ref_last_hit_by,
                                             const uint8_t* source_port0, npy_intp width,
                                             npy_intp row, int players, int p) {
  const int source = (int)last_hit_by[(row * width) + p];
  if (msl_py_local_slot_from_source_port(source_port0, width, row, players, source) >= 0) {
    return source;
  }
  return (int)ref_last_hit_by[(row * width) + p];
}

static inline bool msl_py_action_is_catch_family(uint16_t action_id) {
  return action_id >= (uint16_t)MSL_ACT_CATCH && action_id <= (uint16_t)MSL_ACT_CATCH_CUT;
}

static inline bool msl_py_action_is_basic_grounded_attack(uint16_t action_id) {
  return action_id >= (uint16_t)MSL_ACT_ATTACK_11 && action_id <= (uint16_t)MSL_ACT_ATTACK_LW4;
}

static inline bool msl_py_action_is_grounded_f26_damage_entry_seed_family(uint16_t action_id,
                                                                          int allow_kneebend) {
  return msl_py_action_is_catch_family(action_id) ||
         msl_py_action_is_basic_grounded_attack(action_id) || action_id == (uint16_t)MSL_ACT_DASH ||
         (allow_kneebend != 0 && action_id == (uint16_t)MSL_ACT_KNEE_BEND);
}

static inline bool msl_py_f26_current_pre_action_allows_marker(uint16_t action_id,
                                                               uint8_t on_ground,
                                                               int allow_grounded_kneebend) {
  if (on_ground == 0u) {
    return action_id == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
           action_id == (uint16_t)MSL_ACT_ATTACK_AIR_B;
  }
  return msl_py_action_is_grounded_f26_damage_entry_seed_family(action_id, allow_grounded_kneebend);
}

static int msl_py_f26_current_pre_action_marker(const uint16_t* action, const uint16_t* ref_action,
                                                const uint8_t* ground, const uint16_t* hitlag,
                                                const uint16_t* hitstun, const uint32_t* rng_seed,
                                                npy_intp width, npy_intp row, int p,
                                                int stream_offset_steps, float roll_prob,
                                                int allow_grounded_kneebend) {
  const uint16_t cur = action[(row * width) + p];
  const uint8_t on_ground = ground[(row * width) + p];
  if (hitlag[(row * width) + p] != 0u || hitstun[(row * width) + p] != 0u) {
    return 0;
  }
  if (!msl_py_f26_current_pre_action_allows_marker(cur, on_ground, allow_grounded_kneebend)) {
    return 0;
  }
  float rolls[4];
  for (int consume = 0; consume < 4; consume++) {
    rolls[consume] = msl_py_randf_after_pre_gate(rng_seed[row], stream_offset_steps, consume);
  }
  if (ref_action[(row * width) + p] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
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

static int msl_py_f26_marker_stream_steps(int marker) {
  if (marker <= 0) return 0;
  return (marker == 4 ? 0 : marker) + 1;
}

static int msl_py_f26_prior_same_frame_stream_steps(
    const uint16_t* action, const uint16_t* ref_action, const uint8_t* ground,
    const uint16_t* hitlag, const uint16_t* hitstun, const uint8_t* last_hit_by,
    const uint8_t* ref_last_hit_by, const uint8_t* source_port0, const uint32_t* rng_seed,
    npy_intp width, npy_intp row, int players, int p, float roll_prob,
    int allow_grounded_kneebend) {
  const int cur_source = msl_py_f26_source_port_for_player(last_hit_by, ref_last_hit_by,
                                                           source_port0, width, row, players, p);
  const int cur_attacker =
      msl_py_local_slot_from_source_port(source_port0, width, row, players, cur_source);
  if (cur_attacker < 0) return 0;
  int stream_steps = 0;
  for (int q = 0; q < players; q++) {
    if (q == p) continue;
    const int other_source = msl_py_f26_source_port_for_player(
        last_hit_by, ref_last_hit_by, source_port0, width, row, players, q);
    const int other_attacker =
        msl_py_local_slot_from_source_port(source_port0, width, row, players, other_source);
    if (other_attacker < 0 || other_attacker >= cur_attacker) continue;
    const int marker = msl_py_f26_current_pre_action_marker(
        action, ref_action, ground, hitlag, hitstun, rng_seed, width, row, q, stream_steps,
        roll_prob, allow_grounded_kneebend);
    stream_steps += msl_py_f26_marker_stream_steps(marker);
  }
  return stream_steps;
}

PyObject* msl_derive_fighter_8006cda4_pre_gate_count_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* ref_action_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* all_source_obj = NULL;
  PyObject* all_action_obj = NULL;
  PyObject* all_frame_obj = NULL;
  PyObject* all_ref_action_obj = NULL;
  PyObject* all_ground_obj = NULL;
  PyObject* all_hitlag_obj = NULL;
  PyObject* all_hitstun_obj = NULL;
  PyObject* all_last_hit_by_obj = NULL;
  PyObject* all_ref_last_hit_by_obj = NULL;
  PyObject* rng_obj = NULL;
  double roll_prob_d = 0.0;
  int victim_port = 0;
  int num_players = 0;
  int allow_grounded_kneebend = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOOOdiii", &action_obj, &frame_obj, &ref_action_obj,
                        &ground_obj, &hitlag_obj, &hitstun_obj, &flags_obj, &last_hit_by_obj,
                        &all_source_obj, &all_action_obj, &all_frame_obj, &all_ref_action_obj,
                        &all_ground_obj, &all_hitlag_obj, &all_hitstun_obj, &all_last_hit_by_obj,
                        &all_ref_last_hit_by_obj, &rng_obj, &roll_prob_d, &victim_port,
                        &num_players, &allow_grounded_kneebend)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* ref_action =
      require_contiguous_array(ref_action_obj, NPY_UINT16, 1, "ref_action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* last_hit_by =
      require_contiguous_array(last_hit_by_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* all_source =
      require_contiguous_array(all_source_obj, NPY_UINT8, 2, "all_source_port0_u8");
  PyArrayObject* all_action =
      require_contiguous_array(all_action_obj, NPY_UINT16, 2, "all_action_id_u16");
  PyArrayObject* all_frame =
      require_contiguous_array(all_frame_obj, NPY_INT16, 2, "all_action_frame_i16");
  PyArrayObject* all_ref_action =
      require_contiguous_array(all_ref_action_obj, NPY_UINT16, 2, "all_ref_action_id_u16");
  PyArrayObject* all_ground =
      require_contiguous_array(all_ground_obj, NPY_UINT8, 2, "all_on_ground_u8");
  PyArrayObject* all_hitlag =
      require_contiguous_array(all_hitlag_obj, NPY_UINT16, 2, "all_hitlag_u16");
  PyArrayObject* all_hitstun =
      require_contiguous_array(all_hitstun_obj, NPY_UINT16, 2, "all_hitstun_u16");
  PyArrayObject* all_last_hit_by =
      require_contiguous_array(all_last_hit_by_obj, NPY_UINT8, 2, "all_last_hit_by_u8");
  PyArrayObject* all_ref_last_hit_by =
      require_contiguous_array(all_ref_last_hit_by_obj, NPY_UINT8, 2, "all_ref_last_hit_by_u8");
  PyArrayObject* rng =
      require_contiguous_array(rng_obj, NPY_UINT32, 1, "frame_pre_random_seed_u32");
  if (action == NULL || frame == NULL || ref_action == NULL || ground == NULL || hitlag == NULL ||
      hitstun == NULL || flags == NULL || last_hit_by == NULL || all_source == NULL ||
      all_action == NULL || all_frame == NULL || all_ref_action == NULL || all_ground == NULL ||
      all_hitlag == NULL || all_hitstun == NULL || all_last_hit_by == NULL ||
      all_ref_last_hit_by == NULL || rng == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(ref_action) != n || PyArray_SIZE(ground) != n ||
      PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n || PyArray_SIZE(last_hit_by) != n ||
      PyArray_SIZE(rng) != n || PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "fighter_8006cda4_pre_gate_count 1D inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "fighter_8006cda4_pre_gate_count num_players out of range");
    return NULL;
  }
  if (victim_port < 0 || victim_port >= num_players) {
    PyErr_SetString(PyExc_ValueError, "fighter_8006cda4_pre_gate_count victim_port out of range");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(all_source, 1);
  if (require_exact_2d_shape(all_action, n, width, "all_action_id_u16") < 0 ||
      require_exact_2d_shape(all_frame, n, width, "all_action_frame_i16") < 0 ||
      require_exact_2d_shape(all_ref_action, n, width, "all_ref_action_id_u16") < 0 ||
      require_exact_2d_shape(all_ground, n, width, "all_on_ground_u8") < 0 ||
      require_exact_2d_shape(all_hitlag, n, width, "all_hitlag_u16") < 0 ||
      require_exact_2d_shape(all_hitstun, n, width, "all_hitstun_u16") < 0 ||
      require_exact_2d_shape(all_last_hit_by, n, width, "all_last_hit_by_u8") < 0 ||
      require_exact_2d_shape(all_ref_last_hit_by, n, width, "all_ref_last_hit_by_u8") < 0) {
    return NULL;
  }
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "fighter_8006cda4_pre_gate_count all-player width too small");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;

  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* ref_a = (const uint16_t*)PyArray_DATA(ref_action);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* lhb = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint8_t* source_port = (const uint8_t*)PyArray_DATA(all_source);
  const uint16_t* aa = (const uint16_t*)PyArray_DATA(all_action);
  const int16_t* aaf = (const int16_t*)PyArray_DATA(all_frame);
  const uint16_t* ara = (const uint16_t*)PyArray_DATA(all_ref_action);
  const uint8_t* ag = (const uint8_t*)PyArray_DATA(all_ground);
  const uint16_t* ahl = (const uint16_t*)PyArray_DATA(all_hitlag);
  const uint16_t* ahs = (const uint16_t*)PyArray_DATA(all_hitstun);
  const uint8_t* alhb = (const uint8_t*)PyArray_DATA(all_last_hit_by);
  const uint8_t* arlhb = (const uint8_t*)PyArray_DATA(all_ref_last_hit_by);
  const uint32_t* seed = (const uint32_t*)PyArray_DATA(rng);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const float roll_prob = (float)roll_prob_d;

  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    if (cur == 74u) {
      o[i] = (uint8_t)((af[i] == 0 || af[i] >= 16) ? 2 : 1);
      continue;
    }
    if (cur == 363u) {
      if (af[i] > 3) o[i] = 1u;
      continue;
    }
    if (cur == 57u) {
      o[i] = 1u;
      continue;
    }
    if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_N && g[i] == 0u && hl[i] == 0u && hs[i] == 0u) {
      if (ref_a[i] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
        const int stream_steps = msl_py_f26_prior_same_frame_stream_steps(
            aa, ara, ag, ahl, ahs, alhb, arlhb, source_port, seed, width, i, num_players,
            victim_port, roll_prob, allow_grounded_kneebend);
        const int marker =
            msl_py_f26_current_pre_action_marker(aa, ara, ag, ahl, ahs, seed, width, i, victim_port,
                                                 stream_steps, roll_prob, allow_grounded_kneebend);
        o[i] = (uint8_t)marker;
      }
      continue;
    }
    if (g[i] != 0u && hl[i] == 0u && hs[i] == 0u &&
        (msl_py_action_is_catch_family(cur) ||
         ((msl_py_action_is_basic_grounded_attack(cur) || cur == (uint16_t)MSL_ACT_DASH ||
           (allow_grounded_kneebend != 0 && cur == (uint16_t)MSL_ACT_KNEE_BEND)) &&
          ref_a[i] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL))) {
      const int stream_steps = msl_py_f26_prior_same_frame_stream_steps(
          aa, ara, ag, ahl, ahs, alhb, arlhb, source_port, seed, width, i, num_players, victim_port,
          roll_prob, allow_grounded_kneebend);
      const int marker =
          msl_py_f26_current_pre_action_marker(aa, ara, ag, ahl, ahs, seed, width, i, victim_port,
                                               stream_steps, roll_prob, allow_grounded_kneebend);
      if (marker != 0) o[i] = (uint8_t)marker;
      continue;
    }
    if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_B && g[i] == 0u && hl[i] == 0u && hs[i] == 0u) {
      const int stream_steps = msl_py_f26_prior_same_frame_stream_steps(
          aa, ara, ag, ahl, ahs, alhb, arlhb, source_port, seed, width, i, num_players, victim_port,
          roll_prob, allow_grounded_kneebend);
      const int marker =
          msl_py_f26_current_pre_action_marker(aa, ara, ag, ahl, ahs, seed, width, i, victim_port,
                                               stream_steps, roll_prob, allow_grounded_kneebend);
      if (marker != 0) o[i] = (uint8_t)marker;
      continue;
    }
    if (cur == 239u && g[i] != 0u && hl[i] > 0u && hs[i] == 0u &&
        (sf[(i * sf_w) + 1] & 0x10u) != 0u) {
      o[i] = 2u;
      continue;
    }
    if (cur == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && g[i] == 0u && hl[i] == 0u && hs[i] > 0u) {
      const int attacker =
          msl_py_local_slot_from_source_port(source_port, width, i, num_players, (int)lhb[i]);
      if (attacker < 0 || attacker == victim_port) continue;
      const uint16_t attacker_action = aa[(i * width) + attacker];
      const int attacker_frame = (int)aaf[(i * width) + attacker];
      if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
          (attacker_frame == 3 || attacker_frame == 4)) {
        float rolls[4];
        for (int consume = 0; consume < 4; consume++) {
          rolls[consume] = msl_py_randf_after_pre_gate(seed[i], 0, consume);
        }
        if (ref_a[i] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
          for (int consume = 0; consume < 4; consume++) {
            if (rolls[consume] < roll_prob) {
              o[i] = (uint8_t)(consume > 0 ? consume : 4);
              break;
            }
          }
          continue;
        }
        if (rolls[0] < roll_prob) {
          for (int consume = 1; consume < 4; consume++) {
            if (rolls[consume] >= roll_prob) {
              o[i] = (uint8_t)consume;
              break;
            }
          }
          continue;
        }
      }
      if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_B && attacker_frame >= 6) {
        o[i] = 2u;
      }
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    const int consume = (int)o[i];
    if (consume <= 0 || consume > 4) continue;
    if (!(a[i] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && g[i] == 0u && hl[i] == 0u && hs[i] > 0u)) {
      continue;
    }
    const int attacker =
        msl_py_local_slot_from_source_port(source_port, width, i, num_players, (int)lhb[i]);
    if (attacker < 0 || attacker == victim_port) continue;
    if (aa[(i * width) + attacker] != (uint16_t)MSL_ACT_ATTACK_AIR_B) continue;
    npy_intp j = i - 1;
    while (j >= 0) {
      if (a[j] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) break;
      if (g[j] != 0u || hs[j] == 0u) break;
      if (msl_py_local_slot_from_source_port(source_port, width, j, num_players, (int)lhb[j]) !=
          attacker) {
        break;
      }
      if (o[j] == 0u) o[j] = (uint8_t)consume;
      j--;
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    const int consume = (int)o[i];
    if (consume <= 0 || consume > 3) continue;
    if (!(a[i] == (uint16_t)MSL_ACT_ATTACK_AIR_N && g[i] == 0u && hl[i] == 0u && hs[i] == 0u &&
          ref_a[i] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL)) {
      continue;
    }
    const int source_port_raw = msl_py_f26_source_port_for_player(alhb, arlhb, source_port, width,
                                                                  i, num_players, victim_port);
    const int attacker =
        msl_py_local_slot_from_source_port(source_port, width, i, num_players, source_port_raw);
    if (attacker < 0 || attacker == victim_port) continue;
    const uint16_t attacker_action = aa[(i * width) + attacker];
    npy_intp j = i - 1;
    bool seen_damagefall_handoff = false;
    while (j >= 0) {
      const uint16_t cur = a[j];
      if (cur == (uint16_t)MSL_ACT_ATTACK_AIR_N) {
        if (g[j] != 0u || hl[j] != 0u || hs[j] != 0u) break;
      } else if (cur == (uint16_t)MSL_ACT_DAMAGE_FALL) {
        if (seen_damagefall_handoff) break;
        if (g[j] != 0u || hl[j] != 0u || hs[j] != 0u) break;
        seen_damagefall_handoff = true;
      } else if (cur == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
        if (!seen_damagefall_handoff) break;
        if (g[j] != 0u || hl[j] != 0u || hs[j] <= 0u) break;
      } else {
        break;
      }
      if (msl_py_local_slot_from_source_port(source_port, width, j, num_players, source_port_raw) !=
          attacker) {
        break;
      }
      if (cur != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
          aa[(j * width) + attacker] != attacker_action) {
        break;
      }
      if (o[j] == 0u) o[j] = (uint8_t)consume;
      j--;
    }
  }

  return (PyObject*)out;
}

PyObject* msl_derive_source_clear_processhit_damage_pending_phase_py(PyObject* self,
                                                                     PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* flags_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &action_obj, &flags_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (action == NULL || flags == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_processhit_damage_pending_phase inputs have incompatible shapes");
    return NULL;
  }
  npy_intp dims[1] = {n};
  return PyArray_ZEROS(1, dims, NPY_UINT8, 0);
}

PyObject* msl_derive_phantom_damage_pending_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* percent_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hit_by_obj = NULL;
  PyObject* iid_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOi", &percent_obj, &hitlag_obj, &action_obj, &hit_by_obj,
                        &iid_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 2, "percent_f32");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hit_by =
      require_contiguous_array(hit_by_obj, NPY_UINT16, 2, "instance_hit_by_u16");
  PyArrayObject* iid = require_contiguous_array(iid_obj, NPY_UINT16, 2, "instance_id_u16");
  if (percent == NULL || hitlag == NULL || action == NULL || hit_by == NULL || iid == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(percent, 0);
  const npy_intp width = PyArray_DIM(percent, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(hit_by, n, width, "instance_hit_by_u16") < 0 ||
      require_exact_2d_shape(iid, n, width, "instance_id_u16") < 0) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for player width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_damage = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  PyArrayObject* out_source = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT8, 0);
  if (out_damage == NULL || out_timer == NULL || out_source == NULL) {
    Py_XDECREF(out_damage);
    Py_XDECREF(out_timer);
    Py_XDECREF(out_source);
    return NULL;
  }
  uint8_t* os = (uint8_t*)PyArray_DATA(out_source);
  for (npy_intp i = 0; i < n * 4; i++) os[i] = 0xFFu;
  const float* pct = (const float*)PyArray_DATA(percent);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hb = (const uint16_t*)PyArray_DATA(hit_by);
  const uint16_t* id = (const uint16_t*)PyArray_DATA(iid);
  float* od = (float*)PyArray_DATA(out_damage);
  uint16_t* ot = (uint16_t*)PyArray_DATA(out_timer);
  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      const npy_intp idx = i * width + defender;
      const npy_intp next_idx = (i + 1) * width + defender;
      if (act[idx] != act[next_idx]) continue;
      const float dmg = pct[next_idx] - pct[idx];
      if (!(dmg > 0.0f) || !isfinite(dmg)) continue;
      const uint16_t source_iid = hb[idx];
      if (source_iid == 0u) continue;
      int source_slot = -1;
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        if (id[i * width + attacker] == source_iid) {
          source_slot = attacker;
          break;
        }
      }
      if (source_slot < 0) continue;
      npy_intp start = i;
      while (start >= 0) {
        const npy_intp pidx = start * width + defender;
        if (hl[pidx] == 0u || hb[pidx] != source_iid || fabsf(pct[pidx] - pct[idx]) > 1.0e-5f) {
          break;
        }
        start--;
      }
      start++;
      if (start > i) continue;
      if (start <= 0 || hl[(start - 1) * width + defender] != 0u) continue;
      if (fabsf(pct[start * width + defender] - pct[(start - 1) * width + defender]) > 1.0e-5f) {
        continue;
      }
      for (npy_intp j = start; j <= i; j++) {
        const npy_intp timer = i - j + 1;
        if (timer > 0xFFFF) continue;
        const npy_intp oidx = j * 4 + defender;
        od[oidx] = dmg;
        ot[oidx] = (uint16_t)timer;
        os[oidx] = (uint8_t)source_slot;
      }
    }
  }
  return Py_BuildValue("NNN", out_damage, out_timer, out_source);
}
