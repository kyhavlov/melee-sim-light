/* Native validation item/event materialization. */

#include "msl_validation_items.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/action_ids.h"
#include "../src/api.h"
#include "../src/item_article_params.h"
#include "../src/laser_params.h"
#include "../src/stage_collision.h"

static int require_u8_rows(PyArrayObject* arr, npy_intp rows, npy_intp min_cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s must be uint8[%zd, >=%zd]", name, rows, min_cols);
    return -1;
  }
  return 0;
}

static int table_index_for_value(const float* values, float target, bool absolute_value) {
  if (values == NULL || !isfinite(target)) {
    return -1;
  }
  float best = 1000000.0f;
  int best_i = -1;
  for (int i = 0; i < 8; i++) {
    const float v = absolute_value ? fabsf(values[i]) : values[i];
    const float d = fabsf(v - target);
    if (d < best) {
      best = d;
      best_i = i;
    }
  }
  return best <= 0.0002f ? best_i : -1;
}

static const MslItem* unique_same_item_state4(const MslCompare* ref, const MslItem* seed_item,
                                              uint16_t item_type) {
  if (ref == NULL || seed_item == NULL) {
    return NULL;
  }
  const MslItem* found = NULL;
  for (int ref_it = 0; ref_it < MSL_MAX_ITEMS; ref_it++) {
    const MslItem* candidate = &ref->items[ref_it];
    if (candidate->exists != 0u && candidate->type == item_type &&
        candidate->instance_id == seed_item->instance_id &&
        candidate->spawn_id == seed_item->spawn_id && candidate->owner == seed_item->owner &&
        candidate->state == 4u) {
      if (found != NULL) {
        return NULL;
      }
      found = candidate;
    }
  }
  return found;
}

static bool sheik_needle_derive_state4_motion_lanes(const uint8_t* ref_u8, size_t ref_stride,
                                                    npy_intp rows, npy_intp row_i,
                                                    const MslItem* seed_item,
                                                    const MslItem* current_ref, uint16_t item_type,
                                                    const MslItemArticleParams* ap,
                                                    uint8_t* out_x_idx_sign, uint8_t* out_grav_idx,
                                                    uint8_t* out_min_idx) {
  if (ref_u8 == NULL || seed_item == NULL || current_ref == NULL || ap == NULL ||
      out_x_idx_sign == NULL || out_grav_idx == NULL || out_min_idx == NULL) {
    return false;
  }

  const MslItem* prev = seed_item;
  bool have_x = false;
  bool have_grav = false;
  bool have_min = false;
  uint8_t x_idx_sign = 0u;
  uint8_t grav_idx_u8 = 0u;
  uint8_t min_idx_u8 = 0u;

  // Source owner: state-4 Needle Phys consumes itemVar.seakneedlethrown.xDD8/xDDC/xDE0. Slippi
  // publishes only the public item rows, so a replay seed inside item hitlag can reconstruct these
  // hidden lanes only when later same-spawn state-4 publications prove source Phys integration and
  // the table samples. Limit the future scan to the same item identity; it is a validation/reseed
  // provenance bridge and does not run in free gameplay.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakNeedleThrown_SetupBounce,
  //   itSeakneedlethrown_UnkMotion4_Phys}
  for (npy_intp j = row_i; j < rows && j <= row_i + 96; j++) {
    const MslItem* cur = current_ref;
    if (j != row_i) {
      const MslCompare* future = (const MslCompare*)(const void*)(ref_u8 + (size_t)j * ref_stride);
      cur = unique_same_item_state4(future, seed_item, item_type);
      if (cur == NULL) {
        break;
      }
    }

    const float dx = cur->pos_x - prev->pos_x;
    const float dy = cur->pos_y - prev->pos_y;
    const float dv_y = cur->vel_y - prev->vel_y;
    const bool changed = fabsf(dx) > 0.0002f || fabsf(dy) > 0.0002f ||
                         fabsf(cur->vel_x - prev->vel_x) > 0.0002f ||
                         fabsf(cur->vel_y - prev->vel_y) > 0.0002f;
    if (changed) {
      const int x_idx = table_index_for_value(ap->needle_bounce_x_vel, fabsf(cur->vel_x), false);
      const bool source_pos =
          x_idx >= 0 && fabsf(dx - cur->vel_x) <= 0.0003f && fabsf(dy - cur->vel_y) <= 0.0003f;
      if (!source_pos) {
        break;
      }
      x_idx_sign = (uint8_t)(x_idx | (cur->vel_x < 0.0f ? 0x80u : 0u));
      have_x = true;

      const int grav_idx = table_index_for_value(ap->needle_bounce_gravity, dv_y, false);
      if (grav_idx >= 0) {
        grav_idx_u8 = (uint8_t)grav_idx;
        have_grav = true;
      }
      int min_idx = -1;
      if (grav_idx < 0) {
        min_idx = table_index_for_value(ap->needle_bounce_min_vel_y, cur->vel_y, false);
      } else if (have_grav) {
        const float unclamped_y = prev->vel_y + ap->needle_bounce_gravity[grav_idx_u8];
        if (unclamped_y < cur->vel_y - 0.0002f) {
          min_idx = table_index_for_value(ap->needle_bounce_min_vel_y, cur->vel_y, false);
        }
      }
      if (min_idx >= 0) {
        min_idx_u8 = (uint8_t)min_idx;
        have_min = true;
      }
      if (have_x && have_grav && have_min) {
        *out_x_idx_sign = x_idx_sign;
        *out_grav_idx = grav_idx_u8;
        *out_min_idx = min_idx_u8;
        return true;
      }
    }
    prev = cur;
  }
  return false;
}

PyObject* msl_validation_copy_item_rows_with_illusion_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* ghost_x_obj = NULL;
  PyObject* ghost_y_obj = NULL;
  PyObject* illusion_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOi", &seed_obj, &ref_obj, &items_obj, &action_obj,
                        &hitlag_obj, &instance_hit_by_obj, &ghost_x_obj, &ghost_y_obj,
                        &illusion_lut_obj, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* items = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* hitlag = require_contiguous_array_readonly(hitlag_obj, NPY_UINT16, 2, "hitlag");
  PyArrayObject* instance_hit_by =
      require_contiguous_array_readonly(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by");
  PyArrayObject* ghost_x =
      require_contiguous_array_readonly(ghost_x_obj, NPY_FLOAT32, 2, "ghost_x");
  PyArrayObject* ghost_y =
      require_contiguous_array_readonly(ghost_y_obj, NPY_FLOAT32, 2, "ghost_y");
  PyArrayObject* illusion_lut =
      require_contiguous_array_readonly(illusion_lut_obj, NPY_UINT8, 1, "illusion_item_kind_lut");
  if (seed == NULL || ref == NULL || items == NULL || action == NULL || hitlag == NULL ||
      instance_hit_by == NULL || ghost_x == NULL || ghost_y == NULL || illusion_lut == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(seed, 0);
  const npy_intp frames = n + 1;
  const npy_intp width = PyArray_DIM(action, 1);
  const size_t items_bytes = sizeof(((MslSeed*)0)->items);
  if (n < 1 || PyArray_DIM(ref, 0) != n || PyArray_DIM(items, 0) != frames ||
      PyArray_DIM(items, 1) != (npy_intp)items_bytes ||
      require_u8_rows(seed, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_u8_rows(ref, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      PyArray_NDIM(action) != 2 || PyArray_DIM(action, 0) != frames || PyArray_NDIM(hitlag) != 2 ||
      PyArray_DIM(hitlag, 0) != frames || PyArray_DIM(hitlag, 1) != width ||
      PyArray_NDIM(instance_hit_by) != 2 || PyArray_DIM(instance_hit_by, 0) != frames ||
      PyArray_DIM(instance_hit_by, 1) != width || PyArray_NDIM(ghost_x) != 2 ||
      PyArray_DIM(ghost_x, 0) != frames || PyArray_DIM(ghost_x, 1) != width ||
      PyArray_NDIM(ghost_y) != 2 || PyArray_DIM(ghost_y, 0) != frames ||
      PyArray_DIM(ghost_y, 1) != width || PyArray_SIZE(illusion_lut) < 65536) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError,
                      "validation item illusion buffers have incompatible shapes");
    }
    return NULL;
  }
  if (num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players exceeds replay player field width");
    return NULL;
  }
  const int players = num_players;
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref);
  const uint8_t* items_u8 = (const uint8_t*)PyArray_DATA(items);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* ihb_data = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const float* gx = (const float*)PyArray_DATA(ghost_x);
  const float* gy = (const float*)PyArray_DATA(ghost_y);
  const uint8_t* illusion = (const uint8_t*)PyArray_DATA(illusion_lut);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items, 0);

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed_row = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    MslCompare* ref_row = (MslCompare*)(void*)(ref_u8 + (size_t)i * ref_stride);
    memcpy(seed_row->items, items_u8 + (size_t)i * items_stride, items_bytes);
    memcpy(ref_row->items, items_u8 + (size_t)(i + 1) * items_stride, items_bytes);

    if (i == 0) {
      continue;
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      MslItem* item = &seed_row->items[slot];
      if (item->exists == 0u || illusion[item->type] == 0u) {
        continue;
      }
      const int owner = (int)item->owner;
      if (owner < 0 || owner >= players) {
        continue;
      }
      const uint16_t owner_action = action_data[(i * width) + owner];
      const bool setphys = owner_action == 348u || owner_action == 349u || owner_action == 351u ||
                           owner_action == 352u;
      if (!setphys) {
        continue;
      }

      bool ongoing_guardsetoff_hitlag = false;
      int candidate_victim = -1;
      for (int p = 0; p < players; p++) {
        const npy_intp pidx = (i * width) + p;
        if (hitlag_data[pidx] == 0u || action_data[pidx] != 181u) {
          continue;
        }
        if (candidate_victim >= 0) {
          candidate_victim = -2;
          break;
        }
        candidate_victim = p;
      }
      if (candidate_victim >= 0) {
        int illusion_candidates = 0;
        for (int other = 0; other < MSL_MAX_ITEMS; other++) {
          const MslItem* other_item = &seed_row->items[other];
          if (other_item->exists != 0u && illusion[other_item->type] != 0u) {
            illusion_candidates++;
            if (illusion_candidates > 1) {
              break;
            }
          }
        }
        ongoing_guardsetoff_hitlag = illusion_candidates == 1;
      }

      bool ongoing_body_hitlag = false;
      if (!ongoing_guardsetoff_hitlag) {
        for (int p = 0; p < players; p++) {
          const npy_intp pidx = (i * width) + p;
          if (hitlag_data[pidx] != 0u && ihb_data[pidx] == item->instance_id) {
            ongoing_body_hitlag = true;
            break;
          }
        }
      }
      if (ongoing_guardsetoff_hitlag || ongoing_body_hitlag) {
        continue;
      }
      item->pos_x = gx[(i * width) + owner];
      item->pos_y = gy[(i * width) + owner];
    }
  }
  Py_RETURN_NONE;
}

static const MslItem* previous_same_item(const MslSeed* previous, const MslItem* item,
                                         int* out_slot) {
  if (previous == NULL || item == NULL || item->exists == 0u) {
    return NULL;
  }
  const MslItem* found = NULL;
  int found_slot = -1;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const MslItem* candidate = &previous->items[it];
    if (candidate->exists == 0u || candidate->type != item->type ||
        candidate->instance_id != item->instance_id || candidate->spawn_id != item->spawn_id) {
      continue;
    }
    if (found != NULL) {
      return NULL;
    }
    found = candidate;
    found_slot = it;
  }
  if (found != NULL && out_slot != NULL) {
    *out_slot = found_slot;
  }
  return found;
}

static int unique_guardsetoff_shield_contact(const MslSeed* previous, const MslSeed* current,
                                             int owner, int players) {
  int victim = -1;
  for (int p = 0; p < players; p++) {
    if (p == owner || current->action_id[p] != 181u || current->hitlag[p] == 0u ||
        previous->hitlag[p] != 0u || !(current->shield_hp[p] < previous->shield_hp[p] - 0.0002f)) {
      continue;
    }
    if (victim >= 0) {
      return -1;
    }
    victim = p;
  }
  return victim;
}

static uint8_t laser_hitbox_mask(const MslLaserParams* params, uint8_t state) {
  if (params == NULL) {
    return 0u;
  }
  const uint8_t count =
      state == 0u ? params->hitbox_offsets_x_count : params->state1_hitbox_offsets_x_count;
  const uint8_t* ids = state == 0u ? params->hitbox_ids : params->state1_hitbox_ids;
  uint8_t mask = 0u;
  for (uint8_t i = 0u; i < count; i++) {
    if (ids[i] < (uint8_t)MSL_MAX_HITBOXES) {
      mask |= (uint8_t)(1u << ids[i]);
    }
  }
  return mask;
}

PyObject* msl_validation_derive_item_hitlist_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "Oi", &seed_obj, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  if (seed_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "item hitlist validation inputs are invalid");
    }
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  if (laser_params_init() != 0 || item_article_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "item params are required for item hitlist derivation");
    return NULL;
  }

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      seed->item_hitlist_victim_port[it] = 0xFFu;
      seed->item_hitlist_victim_cd[it] = 0u;
      seed->item_hitlist_victim_hitbox_mask[it] = 0u;
      seed->item_hitlist_victim_iid[it] = 0u;
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* item = &seed->items[it];
      const uint8_t hb_mask =
          laser_hitbox_mask(laser_params_for_item_type(item->type), item->state);
      if (item->exists == 0u || item->state != 1u || hb_mask == 0u || item->instance_id == 0u) {
        continue;
      }
      const int owner = (int)item->owner;
      if (owner < 0 || owner >= players || !msl_action_is_throw_owner(seed->action_id[owner])) {
        continue;
      }
      int candidate = -1;
      for (int victim = 0; victim < players; victim++) {
        if (victim == owner || seed->grab_owner_port[victim] != (uint8_t)owner ||
            !msl_action_is_grabbed_victim(seed->action_id[victim]) || seed->hitlag[victim] == 0u ||
            seed->instance_hit_by[victim] != item->instance_id) {
          continue;
        }
        if (candidate >= 0) {
          candidate = -1;
          break;
        }
        candidate = victim;
      }
      if (candidate < 0) {
        continue;
      }

      // A throw-attached blaster article that is still named by the victim's public damage-source
      // instance while that attached victim is in hitlag has already inserted that fighter into
      // these live item HitCapsules. Hitlag distinguishes that accepted-contact lifetime from the
      // next same-owner laser, whose public instance id is identical but whose new spawn has not
      // hit yet. Slippi omits victims_1 itself; reconstruct the bounded source object on
      // teacher-forced reseed so the same laser cannot be accepted again during its sixteen-frame
      // rehit interval. Normal rollouts create and age the identical rings through
      // items_laser.c / hitlist.c.
      // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_80008440}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      seed->item_hitlist_victim_port[it] = (uint8_t)candidate;
      seed->item_hitlist_victim_cd[it] = 16u;
      seed->item_hitlist_victim_hitbox_mask[it] = hb_mask;
      seed->item_hitlist_victim_iid[it] = seed->instance_id[candidate];
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* item = &seed->items[it];
      if (item->exists == 0u || item->instance_id == 0u || item->owner < 0 ||
          item->owner >= players || item_article_params_body_hit_keeps_article(item->type) == 0u) {
        continue;
      }
      const int owner = (int)item->owner;
      int candidate = -1;
      for (int victim = 0; victim < players; victim++) {
        if (victim == owner || (seed->hitlag[victim] == 0u && seed->hitstun[victim] == 0u) ||
            seed->instance_hit_by[victim] != item->instance_id ||
            seed->last_hit_by[victim] != seed->source_port0[owner]) {
          continue;
        }
        if (candidate >= 0) {
          candidate = -1;
          break;
        }
        candidate = victim;
      }
      if (candidate < 0) {
        continue;
      }

      // BODY-persistent articles keep both the article and their zero-rehit victims_1 entry.
      // Slippi omits victims_1 but exposes the live article and the victim's exact item
      // instance/source while hitlag or hitstun is still active. Restore that real hidden object
      // on teacher-forced reseed; free-running gameplay creates the same entry in the common
      // item-contact caller.
      // refs/melee/src/melee/it/it_279C.c::{itFoxIllusion_Logic14_DmgDealt,
      //   itSeakVanish_Logic42_DmgDealt,itZeldaDinFireExplode}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
      seed->item_hitlist_victim_port[it] = (uint8_t)candidate;
      seed->item_hitlist_victim_cd[it] = 0u;
      seed->item_hitlist_victim_hitbox_mask[it] = 1u;
      seed->item_hitlist_victim_iid[it] = seed->instance_id[candidate];
    }
    if (i == 0) {
      continue;
    }

    const MslSeed* previous =
        (const MslSeed*)(const void*)(seed_u8 + (size_t)(i - 1) * seed_stride);
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      MslItem* item = &seed->items[it];
      const MslLaserParams* laser = laser_params_for_item_type(item->type);
      const bool persistent_body_article =
          item_article_params_body_hit_keeps_article(item->type) != 0u;
      if (item->exists == 0u || (laser == NULL && !persistent_body_article)) {
        continue;
      }
      int previous_slot = -1;
      const MslItem* previous_item = previous_same_item(previous, item, &previous_slot);
      if (previous_item == NULL || previous_item->owner != item->owner ||
          previous_item->state != item->state) {
        continue;
      }

      // Item HitCapsule victims_1 is source-owned persistent state. A zero-rehit shield entry
      // remains resident until the HitCapsule is cleared; carry that exact victim identity while
      // the same item/state owns the capsule. Do not carry finite-cooldown throw attachments here:
      // their producer above reconstructs the currently proven per-hitbox entry directly.
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_80008440}
      // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4}
      const uint8_t previous_victim = previous->item_hitlist_victim_port[previous_slot];
      if (seed->item_hitlist_victim_port[it] == 0xFFu && previous_victim < (uint8_t)players &&
          previous->item_hitlist_victim_cd[previous_slot] == 0u &&
          previous->item_hitlist_victim_hitbox_mask[previous_slot] != 0u) {
        seed->item_hitlist_victim_port[it] = previous_victim;
        seed->item_hitlist_victim_cd[it] = 0u;
        seed->item_hitlist_victim_hitbox_mask[it] =
            previous->item_hitlist_victim_hitbox_mask[previous_slot];
        seed->item_hitlist_victim_iid[it] = previous->item_hitlist_victim_iid[previous_slot];
      }

      if (laser == NULL) {
        continue;
      }

      const int owner = (int)item->owner;
      if (owner < 0 || owner >= players) {
        continue;
      }
      const float dv_x = item->vel_x - previous_item->vel_x;
      const float dv_y = item->vel_y - previous_item->vel_y;
      const float speed2 = item->vel_x * item->vel_x + item->vel_y * item->vel_y;
      if (!isfinite(previous_item->vel_x) || !isfinite(previous_item->vel_y) ||
          !isfinite(item->vel_x) || !isfinite(item->vel_y) ||
          dv_x * dv_x + dv_y * dv_y <= 1.0e-6f || speed2 <= 1.0e-6f) {
        continue;
      }
      const int victim = unique_guardsetoff_shield_contact(previous, seed, owner, players);
      if (victim < 0) {
        continue;
      }

      // A surviving laser whose public velocity changes on the GuardSetOff entry edge exposes the
      // preceding Item_80269DC8 ShieldBounced contact. Source registered the post-motion-entry
      // fighter pointer in every item HitCapsule before invoking the callback; Slippi omits that
      // private ring, so replay reseed materializes it from this causal prefix edge.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_8007925C}
      // refs/melee/src/melee/it/item.c::Item_80269DC8
      // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
      seed->item_hitlist_victim_port[it] = (uint8_t)victim;
      seed->item_hitlist_victim_cd[it] = 0u;
      seed->item_hitlist_victim_hitbox_mask[it] = (uint8_t)((1u << (uint8_t)MSL_MAX_HITBOXES) - 1u);
      seed->item_hitlist_victim_iid[it] = seed->instance_id[victim];
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_derive_sheik_needle_motion_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* needle_lut_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &seed_obj, &ref_obj, &needle_lut_obj)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* needle_lut =
      require_contiguous_array(needle_lut_obj, NPY_UINT8, 1, "needle_type_lut");
  if (seed_arr == NULL || ref_arr == NULL || needle_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (PyArray_DIM(ref_arr, 0) != n ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_u8_rows(ref_arr, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      PyArray_SIZE(needle_lut) < 65536) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "item hidden callback validation inputs are invalid");
    }
    return NULL;
  }
  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* ref_u8 = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  const uint8_t* needle = (const uint8_t*)PyArray_DATA(needle_lut);

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    const MslCompare* ref = (const MslCompare*)(const void*)(ref_u8 + (size_t)i * ref_stride);
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      seed->item_hidden_callback_flags[it] = 0u;
      seed->item_sheik_needle_motion_seed_kind[it] = 0u;
      seed->item_sheik_needle_motion_vel_x_index_sign[it] = 0u;
      seed->item_sheik_needle_motion_gravity_index[it] = 0u;
      seed->item_sheik_needle_motion_min_vel_y_index[it] = 0u;
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* si = &seed->items[it];
      if (si->exists == 0u) {
        continue;
      }
      const uint16_t item_type = si->type;
      const bool is_needle = needle[item_type] != 0u;
      if (!is_needle) {
        continue;
      }
      if (is_needle && si->state == 4u) {
        const MslItemArticleParams* ap =
            item_article_params_for_sheik_needle_throw_item_type(item_type);
        const MslItem* motion_ref = NULL;
        for (int ref_it = 0; ref_it < MSL_MAX_ITEMS; ref_it++) {
          const MslItem* candidate = &ref->items[ref_it];
          if (candidate->exists != 0u && candidate->type == item_type &&
              candidate->instance_id == si->instance_id && candidate->spawn_id == si->spawn_id &&
              candidate->state == 4u && candidate->owner == si->owner) {
            if (motion_ref != NULL) {
              motion_ref = NULL;
              break;
            }
            motion_ref = candidate;
          }
        }
        if (ap != NULL && motion_ref != NULL) {
          const float dx = motion_ref->pos_x - si->pos_x;
          const float dy = motion_ref->pos_y - si->pos_y;
          const float dv_y = motion_ref->vel_y - si->vel_y;
          const bool unchanged = fabsf(dx) <= 0.0002f && fabsf(dy) <= 0.0002f &&
                                 fabsf(motion_ref->vel_x - si->vel_x) <= 0.0002f &&
                                 fabsf(motion_ref->vel_y - si->vel_y) <= 0.0002f &&
                                 ((si->vel_x * si->vel_x) + (si->vel_y * si->vel_y)) > 1.0e-6f;
          if (unchanged) {
            // Hidden item hitlag (`item->xCBC_hitlagFrames`) freezes Item_802697D4 before state-4
            // Phys. A same-identity state-4 item with nonzero public velocity but no publication
            // change proves the one-step reseed started inside that hidden freeze. When later
            // same-spawn publications prove the hidden SetupBounce lanes, carry them too so normal
            // state-4 Phys resumes with source xDD8/xDDC/xDE0 after hitlag clears.
            // refs/melee/src/melee/it/item.c::{checkHitLag,Item_802697D4}
            // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
            //   itSeakNeedleThrown_SetupBounce,itSeakneedlethrown_UnkMotion4_Phys}
            uint8_t x_idx_sign = 0u;
            uint8_t grav_idx = 0u;
            uint8_t min_idx = 0u;
            if (sheik_needle_derive_state4_motion_lanes(ref_u8, ref_stride, n, i, si, motion_ref,
                                                        item_type, ap, &x_idx_sign, &grav_idx,
                                                        &min_idx)) {
              seed->item_sheik_needle_motion_seed_kind[it] = 3u;
              seed->item_sheik_needle_motion_vel_x_index_sign[it] = x_idx_sign;
              seed->item_sheik_needle_motion_gravity_index[it] = grav_idx;
              seed->item_sheik_needle_motion_min_vel_y_index[it] = min_idx;
            } else {
              seed->item_sheik_needle_motion_seed_kind[it] = 2u;
            }
          } else {
            const int x_idx =
                table_index_for_value(ap->needle_bounce_x_vel, fabsf(motion_ref->vel_x), false);
            const int grav_idx = table_index_for_value(ap->needle_bounce_gravity, dv_y, false);
            const bool source_pos = x_idx >= 0 && fabsf(dx - motion_ref->vel_x) <= 0.0003f &&
                                    fabsf(dy - motion_ref->vel_y) <= 0.0003f;
            if (source_pos && grav_idx >= 0) {
              // State-4 Phys consumes hidden itemVar xDD8/xDE0 every frame; Slippi publishes only
              // the visible item velocity. Restore the source-table samples when the next
              // same-item publication proves an unclamped Phys update.
              // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion4_Phys
              seed->item_sheik_needle_motion_seed_kind[it] = 1u;
              seed->item_sheik_needle_motion_vel_x_index_sign[it] =
                  (uint8_t)(x_idx | (motion_ref->vel_x < 0.0f ? 0x80u : 0u));
              seed->item_sheik_needle_motion_gravity_index[it] = (uint8_t)grav_idx;
            } else if (source_pos) {
              const int min_idx =
                  table_index_for_value(ap->needle_bounce_min_vel_y, motion_ref->vel_y, false);
              if (min_idx >= 0) {
                // Same source Phys callback, but without a proven xDE0 gravity sample this row does
                // not identify the full xDD8/xDDC/xDE0 tuple. Leave it unbridged instead of
                // fabricating terminal-y hidden state from the public clamped velocity alone.
                // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion4_Phys
                (void)min_idx;
              }
            }
          }
        }
      }
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_derive_item_reflect_damage_mul_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* reflector_lut_obj = NULL;
  double powershield_mul = 1.0;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOdi", &seed_obj, &items_obj, &action_obj, &char_obj, &flags_obj,
                        &reflector_lut_obj, &powershield_mul, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* items_arr = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* flags = require_contiguous_array_readonly(flags_obj, NPY_UINT8, 3, "state_flags");
  PyArrayObject* reflector_lut =
      require_contiguous_array_readonly(reflector_lut_obj, NPY_FLOAT32, 1, "reflector_lut");
  if (seed_arr == NULL || items_arr == NULL || action == NULL || chr == NULL || flags == NULL ||
      reflector_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  const npy_intp frames = n + 1;
  const npy_intp width = PyArray_DIM(action, 1);
  const size_t items_bytes = sizeof(((MslSeed*)0)->items);
  if (PyArray_DIM(items_arr, 0) != frames || PyArray_DIM(items_arr, 1) != (npy_intp)items_bytes ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      PyArray_NDIM(action) != 2 || PyArray_DIM(action, 0) != frames || PyArray_NDIM(chr) != 2 ||
      PyArray_DIM(chr, 0) != frames || PyArray_DIM(chr, 1) != width || PyArray_NDIM(flags) != 3 ||
      PyArray_DIM(flags, 0) != frames || PyArray_DIM(flags, 1) != width ||
      PyArray_DIM(flags, 2) < 4 || PyArray_SIZE(reflector_lut) < 256) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "item reflect validation buffers are invalid");
    }
    return NULL;
  }
  if (num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players exceeds replay player field width");
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* items_u8 = (const uint8_t*)PyArray_DATA(items_arr);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* reflector = (const float*)PyArray_DATA(reflector_lut);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items_arr, 0);
  const npy_intp flags_depth = PyArray_DIM(flags, 2);

  uint32_t active_spawn[MSL_MAX_ITEMS] = {0};
  uint16_t active_type[MSL_MAX_ITEMS] = {0};
  float active_mul[MSL_MAX_ITEMS] = {0.0f};
  int active_owner[MSL_MAX_ITEMS] = {0};
  int active_iid[MSL_MAX_ITEMS] = {0};
  float active_vx[MSL_MAX_ITEMS] = {0.0f};
  float active_vy[MSL_MAX_ITEMS] = {0.0f};
  bool active_valid[MSL_MAX_ITEMS] = {false};

  for (npy_intp i = 0; i < frames; i++) {
    bool seen[MSL_MAX_ITEMS] = {false};
    const MslItem* items = (const MslItem*)(const void*)(items_u8 + (size_t)i * items_stride);
    MslSeed* seed = i < n ? (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride) : NULL;
    if (seed != NULL) {
      for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) seed->item_reflect_damage_mul[slot] = 1.0f;
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      const MslItem* item = &items[slot];
      if (item->exists == 0u) continue;
      int active_idx = -1;
      for (int k = 0; k < MSL_MAX_ITEMS; k++) {
        if (active_valid[k] && active_spawn[k] == item->spawn_id && active_type[k] == item->type) {
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
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = item->spawn_id;
        active_type[active_idx] = item->type;
        active_mul[active_idx] = 1.0f;
        active_owner[active_idx] = (int)item->owner;
        active_iid[active_idx] = (int)item->instance_id;
        active_vx[active_idx] = item->vel_x;
        active_vy[active_idx] = item->vel_y;
      }
      float mul = active_mul[active_idx];
      const int owner = (int)item->owner;
      const int prev_owner = active_owner[active_idx];
      const int prev_iid = active_iid[active_idx];
      const float prev_vx = active_vx[active_idx];
      const float prev_vy = active_vy[active_idx];
      const bool owner_changed = owner != prev_owner;
      const bool instance_transfer = (int)item->instance_id != prev_iid;
      const float prev_speed_sq = prev_vx * prev_vx + prev_vy * prev_vy;
      const float cur_speed_sq = item->vel_x * item->vel_x + item->vel_y * item->vel_y;
      const bool reversed_vel_same_owner =
          owner == prev_owner && !instance_transfer && prev_speed_sq > 1.0e-6f &&
          cur_speed_sq > 1.0e-6f && (prev_vx * item->vel_x + prev_vy * item->vel_y) < 0.0f &&
          fabsf(cur_speed_sq - prev_speed_sq) <= 0.25f * fmaxf(prev_speed_sq, cur_speed_sq);
      if ((owner_changed || instance_transfer || reversed_vel_same_owner) && owner >= 0 &&
          owner < players) {
        const npy_intp fighter_idx = (i * width) + owner;
        const uint16_t act = action_data[fighter_idx];
        const uint8_t flags3 = flags_data[(fighter_idx * flags_depth) + 3];
        if (act == 0x00B6u && (flags3 & 0x20u)) {
          mul = powershield_mul > 0.0 ? (float)powershield_mul : 1.0f;
        } else {
          const float char_mul = reflector[char_data[fighter_idx]];
          const bool zelda_nayru_owner =
              char_data[fighter_idx] == 19u && (act == 341u || act == 342u);
          if (char_data[fighter_idx] != 19u || zelda_nayru_owner) {
            if (char_mul > 0.0f) mul = char_mul;
          }
        }
      }
      active_mul[active_idx] = mul;
      active_owner[active_idx] = owner;
      active_iid[active_idx] = (int)item->instance_id;
      active_vx[active_idx] = item->vel_x;
      active_vy[active_idx] = item->vel_y;
      seen[active_idx] = true;
      if (seed != NULL) seed->item_reflect_damage_mul[slot] = mul;
    }
    for (int k = 0; k < MSL_MAX_ITEMS; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  Py_RETURN_NONE;
}

typedef struct MslValidationZeldaDinTrack {
  bool valid;
  uint16_t type;
  uint32_t spawn_id;
  int owner;
  uint16_t instance_id;
  uint8_t state;
  float charge;
} MslValidationZeldaDinTrack;

static int zelda_din_find_track_by_public_identity(MslValidationZeldaDinTrack* tracks,
                                                   uint16_t type, uint32_t spawn_id) {
  for (int k = 0; k < MSL_MAX_ITEMS; k++) {
    if (tracks[k].valid && tracks[k].type == type && tracks[k].spawn_id == spawn_id) {
      return k;
    }
  }
  return -1;
}

static int zelda_din_find_track_by_owner_instance(MslValidationZeldaDinTrack* tracks, int owner,
                                                  uint16_t instance_id) {
  for (int k = 0; k < MSL_MAX_ITEMS; k++) {
    if (tracks[k].valid && tracks[k].owner == owner && tracks[k].instance_id == instance_id) {
      return k;
    }
  }
  return -1;
}

static int zelda_din_alloc_track(MslValidationZeldaDinTrack* tracks) {
  for (int k = 0; k < MSL_MAX_ITEMS; k++) {
    if (!tracks[k].valid) {
      tracks[k].valid = true;
      return k;
    }
  }
  tracks[0].valid = true;
  return 0;
}

static float zelda_din_clamp_charge(float charge, float max_charge) {
  if (!isfinite(charge) || charge < 0.0f) {
    charge = 0.0f;
  }
  if (max_charge > 0.0f && charge > max_charge) {
    charge = max_charge;
  }
  return charge;
}

PyObject* msl_validation_derive_zelda_din_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* items_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOi", &seed_obj, &items_obj, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* items_arr = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  if (seed_arr == NULL || items_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      PyArray_DIM(items_arr, 0) < n ||
      require_u8_rows(items_arr, PyArray_DIM(items_arr, 0),
                      (npy_intp)(sizeof(MslItem) * MSL_MAX_ITEMS), "items_u8") != 0) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "Zelda Din validation inputs are invalid");
    }
    return NULL;
  }
  if (item_article_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "item_article_params_init failed");
    return NULL;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)19u);
  if (ap == NULL || ap->zelda_din_fire_itkind == 0u || ap->zelda_din_fire_explode_itkind == 0u) {
    PyErr_SetString(PyExc_RuntimeError, "Zelda Din item article params are unavailable");
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* items_u8 = (const uint8_t*)PyArray_DATA(items_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items_arr, 0);
  MslValidationZeldaDinTrack tracks[MSL_MAX_ITEMS] = {0};

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    const MslItem* items = (const MslItem*)(const void*)(items_u8 + (size_t)i * items_stride);
    bool seen[MSL_MAX_ITEMS] = {false};
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      seed->item_zelda_din_charge[it] = 0.0f;
      seed->item_zelda_din_angle_offset[it] = 0.0f;
      seed->item_zelda_din_base_angle[it] = 0.0f;
      seed->item_zelda_din_speed[it] = 0.0f;
      seed->item_zelda_din_explode_base_size[it] = 0.0f;
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* item = &items[it];
      if (item->exists == 0u) {
        continue;
      }
      const bool is_fire = item->type == ap->zelda_din_fire_itkind;
      const bool is_explode = item->type == ap->zelda_din_fire_explode_itkind;
      if (!is_fire && !is_explode) {
        continue;
      }
      int ti = zelda_din_find_track_by_public_identity(tracks, item->type, item->spawn_id);
      if (ti < 0) {
        ti = zelda_din_find_track_by_owner_instance(tracks, (int)item->owner, item->instance_id);
      }
      if (ti < 0) {
        ti = zelda_din_alloc_track(tracks);
        tracks[ti].charge = 0.0f;
        tracks[ti].state = 0xFFu;
      }
      const uint8_t prev_state = tracks[ti].state;
      tracks[ti].valid = true;
      tracks[ti].type = item->type;
      tracks[ti].spawn_id = item->spawn_id;
      tracks[ti].owner = (int)item->owner;
      tracks[ti].instance_id = item->instance_id;
      if (is_fire && item->state == 0u) {
        // Replay/reseed bridge for Din fire itemVar.xDD8:
        // itZeldadinfire_UnkMotion0_Anim increments xDD8 once per non-reflected state-0 item
        // frame while the public life timer counts down from article attrs.x0. Use that visible
        // timer history to initialize the hidden charge lane for mid-article reseeds.
        // refs/melee/src/melee/it/items/itzeldadinfire.c::{
        //   it_802C3BAC,it_802C3D74,itZeldadinfire_UnkMotion0_Anim}
        const float visible_charge = (float)ap->zelda_din_fire_lifetime_frames - item->timer;
        tracks[ti].charge =
            zelda_din_clamp_charge(visible_charge, ap->zelda_din_fire_charge_max_frames);
      } else {
        if (prev_state == 0u) {
          // State 0's Anim increments xDD8 before `ftZd_SpecialLw_8013B574` can switch the item
          // to release state 1. The first visible state-1/explosion row therefore owns one more
          // hidden charge frame than the previous public state-0 timer exposes.
          // refs/melee/src/melee/it/items/itzeldadinfire.c::itZeldadinfire_UnkMotion0_Anim
          tracks[ti].charge += 1.0f;
        }
        tracks[ti].charge =
            zelda_din_clamp_charge(tracks[ti].charge, ap->zelda_din_fire_charge_max_frames);
      }
      tracks[ti].state = item->state;
      seen[ti] = true;
      seed->item_zelda_din_charge[it] = tracks[ti].charge;
      const float vx = item->vel_x;
      const float vy = item->vel_y;
      seed->item_zelda_din_speed[it] = sqrtf(vx * vx + vy * vy);
      const float dir = item->direction < 0.0f ? -1.0f : 1.0f;
      const float base_angle = dir > 0.0f ? 0.0f : 3.14159265358979323846f;
      seed->item_zelda_din_base_angle[it] = base_angle;
      seed->item_zelda_din_angle_offset[it] = atan2f(vy, vx) - base_angle;
      if (is_explode) {
        // Explosion itemVar.xDD4 is copied from the fire charge by it_802C4580; Slippi exposes
        // only the new explosion item. Carry the same tracked charge by owner/instance identity.
        // refs/melee/src/melee/it/items/itzeldadinfire.c::itZeldadinfire_UnkMotion1_Anim
        // refs/melee/src/melee/it/items/itzeldadinfireexplode.c::it_802C4580
        seed->item_zelda_din_explode_base_size[it] = ap->zelda_din_explode_hitbox_size;
      }
    }
    for (int k = 0; k < MSL_MAX_ITEMS; k++) {
      if (tracks[k].valid && !seen[k]) {
        tracks[k].valid = false;
      }
    }
  }
  Py_RETURN_NONE;
}
