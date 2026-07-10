/* Native validation derivation for fighter special/action seed lanes. */

#include "msl_validation_specials.h"
#include "msl_validation_history_common.h"

#include "../src/anim_pose.h"
#include "../src/anim_table.h"
#include "../src/action_ids.h"
#include "../src/char_params.h"
#include "../src/char_registry.h"
#include "../src/common_params.h"
#include "../src/hitboxes_tables.h"
#include "../src/sheik_specials.h"

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

static inline uint8_t msl_py_action_is_sheik_needle_start(uint16_t action) {
  return action == 341u || action == 345u;
}

static inline uint8_t msl_py_action_is_sheik_needle_loop(uint16_t action) {
  return action == 342u || action == 346u;
}

static inline uint8_t msl_py_action_is_sheik_needle_cancel(uint16_t action) {
  return action == 343u || action == 347u;
}

static inline uint8_t msl_py_action_is_sheik_needle_end(uint16_t action) {
  return action == 344u || action == 348u;
}

static inline uint8_t msl_py_action_is_sheik_needle_family(uint16_t action) {
  return msl_py_action_is_sheik_needle_start(action) ||
         msl_py_action_is_sheik_needle_loop(action) ||
         msl_py_action_is_sheik_needle_cancel(action) || msl_py_action_is_sheik_needle_end(action);
}

// Defined below with the other Side-B Chain predicates; forward-declared here so the Needle
// seed-lane derivation can reference the shared owner instead of a duplicate range check.
static inline uint8_t msl_py_action_is_sheik_chain_family(uint16_t action);

static inline uint8_t msl_py_action_clears_sheik_needle_damage_callback(uint16_t action) {
  // Damage/Dead/respawn entry actions on which ftCommon_8007DB58 runs the installed take_dmg/death
  // callback: DeadDown..DeadUpFallHitCameraFlip (0..10), Rebirth (0x26), the Damage* family
  // (0x4B..0x5B), DownDamage variants (0xB9, 0xC1), and DamageScrew (0x145). This is a raw common
  // action-id set because the native preprocessor has no generated common-MotionState classifier for
  // "take-damage/death callback-entry" actions; the runtime owner is the installed callback, and
  // this list only matters in combination with the caller's callback-installed gate (prev action is
  // SpecialN, or SpecialS with a live Chain article). It is the least-bad native-side representation
  // until a shared damage/death entry predicate is exported to preprocessing.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DB58
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialN_80111FBC
  return (uint8_t)((action <= 10u) || action == 0x26u || (action >= 0x4Bu && action <= 0x5Bu) ||
                   action == 0xB9u || action == 0xC1u || action == 0x145u);
}

static inline uint8_t msl_py_sheik_needle_end_shoot_frame(int16_t frame) {
  return frame == 2 || frame == 5 || frame == 8 || frame == 11 || frame == 14 || frame == 17;
}

PyObject* msl_derive_sheik_needle_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* chain_present_obj = NULL;
  int sheik_internal_id = -1;
  if (!PyArg_ParseTuple(args, "OOOOi", &char_obj, &action_obj, &frame_obj, &chain_present_obj,
                        &sheik_internal_id)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 2, "action_frame_i16");
  PyArrayObject* chain_present =
      require_contiguous_array(chain_present_obj, NPY_UINT8, 2, "sheik_chain_article_present_u8");
  if (chr == NULL || action == NULL || frame == NULL || chain_present == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  const npy_intp width = PyArray_DIM(chr, 1);
  if (require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(frame, n, width, "action_frame_i16") < 0 ||
      require_exact_2d_shape(chain_present, n, width, "sheik_chain_article_present_u8") < 0) {
    return NULL;
  }

  PyArrayObject* out_count = (PyArrayObject*)PyArray_ZEROS(2, PyArray_DIMS(chr), NPY_UINT8, 0);
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(2, PyArray_DIMS(chr), NPY_UINT8, 0);
  if (out_count == NULL || out_timer == NULL) {
    Py_XDECREF(out_count);
    Py_XDECREF(out_timer);
    return NULL;
  }
  if (sheik_internal_id < 0 || sheik_internal_id > 255) {
    return Py_BuildValue("NN", out_count, out_timer);
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* chain_present_in = (const uint8_t*)PyArray_DATA(chain_present);
  uint8_t* count_out = (uint8_t*)PyArray_DATA(out_count);
  uint8_t* timer_out = (uint8_t*)PyArray_DATA(out_timer);
  const uint8_t sheik_id = (uint8_t)sheik_internal_id;

  for (npy_intp p = 0; p < width; p++) {
    uint8_t count = 0u;
    uint8_t needle_damage_callback_installed = 0u;
    uint16_t prev_action = 0u;
    int16_t prev_frame = -1;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp idx = (i * width) + p;
      const uint16_t action_i = act[idx];
      const int16_t frame_i = af[idx];
      const uint8_t chain_present_i = (uint8_t)(chain_present_in[idx] != 0u);
      if (ch[idx] != sheik_id) {
        count = 0u;
        needle_damage_callback_installed = 0u;
        prev_action = action_i;
        prev_frame = frame_i;
        continue;
      }
      if (!msl_py_action_is_sheik_needle_family(action_i)) {
        // fv.sk.x0 is stored Needle count, not SpecialN-local motion state. It persists after
        // Cancel/End exits and across unrelated Sheik actions until a shootNeedles decrement or a
        // still-installed damage/death callback clears it.
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
        //   doEnter,ftSk_SpecialNLoop_Anim,ftSk_SpecialNEnd_Anim,shootNeedles,
        //   ftSk_SpecialN_80111FBC}
        // The take_dmg/death callback is hidden Fighter state, but ordinary motion changes clear
        // it in Fighter_ChangeMotionState. Therefore it can bridge into an immediate damage/death
        // entry from SpecialN/Chain, but it does not survive an intervening common action while
        // the stored count itself does.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_80110198
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
        //   setDmgCallbacks,ftSk_SpecialN_80111FBC,ftSk_SpecialN_801120D4}
        if (needle_damage_callback_installed != 0u &&
            msl_py_action_clears_sheik_needle_damage_callback(action_i)) {
          count = 0u;
          needle_damage_callback_installed = 0u;
        } else if (msl_py_action_is_sheik_chain_family(action_i) && chain_present_i != 0u) {
          // Several Chain active callbacks install the same ftSk_Init_80110198 damage/death
          // callback while fv.sk.x8 points at the live Chain article.
          // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c
          needle_damage_callback_installed = 1u;
        } else {
          needle_damage_callback_installed = 0u;
        }
        count_out[idx] = count;
        prev_action = action_i;
        prev_frame = frame_i;
        continue;
      }

      uint8_t timer = 0u;
      if (msl_py_action_is_sheik_needle_start(action_i)) {
        if (!msl_py_action_is_sheik_needle_family(prev_action) && count == 0u) {
          count = 1u;
        }
        needle_damage_callback_installed = 1u;
      } else if (msl_py_action_is_sheik_needle_loop(action_i)) {
        if (count == 0u) {
          count = 1u;
        }
        needle_damage_callback_installed = 1u;
        if (msl_py_action_is_sheik_needle_loop(prev_action) && frame_i == 0 && prev_frame > 0 &&
            count < 6u) {
          count++;
        }
      } else if (msl_py_action_is_sheik_needle_cancel(action_i)) {
        needle_damage_callback_installed = 1u;
      } else {
        // End rows seed mv.sk.specialn.x0 before this row's End_Anim callback. A fresh Loop->End
        // handoff starts at zero; subsequent End rows expose their post-frame action_frame.
        //
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
        //   doEnter,ftSk_SpecialNLoop_Anim,doIasa,ftSk_SpecialNEnd_Anim,shootNeedles}
        timer = msl_py_action_is_sheik_needle_loop(prev_action)
                    ? 0u
                    : (uint8_t)(frame_i < 0 ? 0 : (frame_i > 255 ? 255 : frame_i));
        needle_damage_callback_installed = 1u;
      }

      count_out[idx] = count;
      timer_out[idx] = timer;
      if (msl_py_action_is_sheik_needle_end(action_i) &&
          msl_py_sheik_needle_end_shoot_frame(frame_i) && count > 0u) {
        count--;
      }
      prev_action = action_i;
      prev_frame = frame_i;
    }
  }

  return Py_BuildValue("NN", out_count, out_timer);
}

static inline uint8_t msl_py_action_is_sheik_chain_start(uint16_t action) {
  return action == 349u || action == 352u;
}

static inline uint8_t msl_py_action_is_sheik_chain_active(uint16_t action) {
  return action == 350u || action == 353u;
}

static inline uint8_t msl_py_action_is_sheik_chain_end(uint16_t action) {
  return action == 351u || action == 354u;
}

static inline uint8_t msl_py_action_is_sheik_chain_family(uint16_t action) {
  return msl_py_action_is_sheik_chain_start(action) ||
         msl_py_action_is_sheik_chain_active(action) || msl_py_action_is_sheik_chain_end(action);
}

static inline uint8_t msl_py_saturating_inc_u8(uint8_t x) {
  return x == UINT8_MAX ? UINT8_MAX : (uint8_t)(x + 1u);
}

PyObject* msl_derive_sheik_chain_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* buttons_obj = NULL;
  PyObject* hitlag_obj = NULL;
  int sheik_internal_id = -1;
  unsigned int b_mask_in = 0u;
  int release_min_frames = 0;
  if (!PyArg_ParseTuple(args, "OOOOiIi", &char_obj, &action_obj, &buttons_obj, &hitlag_obj,
                        &sheik_internal_id, &b_mask_in, &release_min_frames)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 2, "buttons_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  if (chr == NULL || action == NULL || buttons == NULL || hitlag == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  const npy_intp width = PyArray_DIM(chr, 1);
  if (require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(buttons, n, width, "buttons_u16") < 0 ||
      require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0) {
    return NULL;
  }

  PyArrayObject* out_x0 = (PyArrayObject*)PyArray_ZEROS(2, PyArray_DIMS(chr), NPY_UINT8, 0);
  PyArrayObject* out_latch = (PyArrayObject*)PyArray_ZEROS(2, PyArray_DIMS(chr), NPY_UINT8, 0);
  if (out_x0 == NULL || out_latch == NULL) {
    Py_XDECREF(out_x0);
    Py_XDECREF(out_latch);
    return NULL;
  }
  if (sheik_internal_id < 0 || sheik_internal_id > 255) {
    return Py_BuildValue("NN", out_x0, out_latch);
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* held_buttons = (const uint16_t*)PyArray_DATA(buttons);
  const uint16_t* hitlag_frames = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* x0_out = (uint8_t*)PyArray_DATA(out_x0);
  uint8_t* latch_out = (uint8_t*)PyArray_DATA(out_latch);
  const uint8_t sheik_id = (uint8_t)sheik_internal_id;
  const uint16_t b_mask = (uint16_t)b_mask_in;
  const uint8_t release_min =
      release_min_frames <= 0
          ? 0u
          : (release_min_frames > 255 ? UINT8_MAX : (uint8_t)release_min_frames);

  for (npy_intp p = 0; p < width; p++) {
    uint8_t x0 = 0u;
    uint8_t latch = 0u;
    uint16_t prev_action = 0u;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp idx = (i * width) + p;
      const uint16_t action_i = act[idx];
      const uint8_t chain_i = ch[idx] == sheik_id && msl_py_action_is_sheik_chain_family(action_i);
      if (!chain_i) {
        x0 = 0u;
        latch = 0u;
        prev_action = action_i;
        continue;
      }

      if (msl_py_action_is_sheik_chain_start(action_i)) {
        if (!msl_py_action_is_sheik_chain_start(prev_action)) {
          x0 = 0u;
        }
        latch = 0u;
      } else if (msl_py_action_is_sheik_chain_active(action_i)) {
        if (!msl_py_action_is_sheik_chain_active(prev_action)) {
          x0 = 0u;
          latch = 0u;
        }
      } else if (!msl_py_action_is_sheik_chain_end(prev_action)) {
        x0 = 0u;
        latch = 0u;
      }

      x0_out[idx] = x0;
      latch_out[idx] = latch;

      // Chain's timers are owned by Anim/IASA callbacks, not by the replay-visible action row.
      // Fighter_8006A1BC decrements hitlag at proc prio 0 before Fighter_8006A360's non-hitlag
      // update path. A frame-start replay seed with hitlag > 1 therefore remains callback-frozen
      // for the derived next seed; the terminal tick (1 -> 0) is the post-hitlag callback-visible
      // frame.
      //
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialS_CheckInitChain,ftSk_SpecialS_Anim,ftSk_SpecialS_IASA}
      if (hitlag_frames[idx] > 1u) {
        prev_action = action_i;
        continue;
      }

      if (msl_py_action_is_sheik_chain_start(action_i)) {
        x0 = msl_py_saturating_inc_u8(x0);
      } else if (msl_py_action_is_sheik_chain_active(action_i)) {
        const uint8_t next_x0 = msl_py_saturating_inc_u8(x0);
        const uint8_t exits = (uint8_t)(next_x0 > release_min && latch != 0u);
        x0 = next_x0;
        if (!exits && (held_buttons[idx] & b_mask) == 0u) {
          latch = 1u;
        }
      } else {
        x0 = msl_py_saturating_inc_u8(x0);
        latch = 0u;
      }
      prev_action = action_i;
    }
  }

  return Py_BuildValue("NN", out_x0, out_latch);
}

PyObject* msl_derive_zelda_twin_state_flags_2218_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* state_flags_obj = NULL;
  int zelda_internal_id = -1;
  if (!PyArg_ParseTuple(args, "OOi", &char_obj, &state_flags_obj, &zelda_internal_id)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* flags = require_contiguous_array(state_flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (chr == NULL || flags == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  if (PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 1) {
    PyErr_SetString(PyExc_ValueError, "state_flags_u8 must have shape (n, >=1)");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) {
    return NULL;
  }
  if (zelda_internal_id < 0 || zelda_internal_id > 255) {
    return (PyObject*)out;
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* state_flags = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp flag_stride = PyArray_DIM(flags, 1);
  uint8_t* out_flags = (uint8_t*)PyArray_DATA(out);
  const uint8_t zelda_id = (uint8_t)zelda_internal_id;
  uint8_t cached = 0u;
  for (npy_intp i = 0; i < n; i++) {
    if (ch[i] == zelda_id) {
      cached = state_flags[(i * flag_stride) + 0];
    }
    out_flags[i] = cached;
  }
  return (PyObject*)out;
}

static inline uint8_t msl_py_action_is_teleport_air_start1(uint8_t char_id, uint16_t action) {
  if (char_id == (uint8_t)MSL_CHAR_ID_SHEIK) {
    return action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_HI_START_1;
  }
  if (char_id == (uint8_t)MSL_CHAR_ID_ZELDA) {
    return action == (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_HI_START_1;
  }
  return 0u;
}

PyObject* msl_derive_sheik_vanish_floor_skip_segments_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* ground_id_obj = NULL;
  PyObject* timer_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* platform_ids_obj = NULL;
  PyObject* platform_x0_obj = NULL;
  PyObject* platform_y0_obj = NULL;
  PyObject* platform_x1_obj = NULL;
  PyObject* platform_y1_obj = NULL;
  int sheik_internal_id = -1;
  int travel_frames = 0;
  double ground_contact_min_frames = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOiid", &char_obj, &action_obj, &on_ground_obj,
                        &ground_id_obj, &timer_obj, &pos_x_obj, &pos_y_obj, &platform_ids_obj,
                        &platform_x0_obj, &platform_y0_obj, &platform_x1_obj, &platform_y1_obj,
                        &sheik_internal_id, &travel_frames, &ground_contact_min_frames)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* ground_id =
      require_contiguous_array(ground_id_obj, NPY_UINT16, 2, "ground_id_u16");
  PyArrayObject* timer =
      require_contiguous_array(timer_obj, NPY_UINT8, 2, "vanish_travel_timer_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "pos_y_f32");
  PyArrayObject* platform_ids =
      require_contiguous_array(platform_ids_obj, NPY_UINT16, 1, "platform_segment_ids_u16");
  PyArrayObject* platform_x0 =
      require_contiguous_array(platform_x0_obj, NPY_FLOAT32, 1, "platform_x0_f32");
  PyArrayObject* platform_y0 =
      require_contiguous_array(platform_y0_obj, NPY_FLOAT32, 1, "platform_y0_f32");
  PyArrayObject* platform_x1 =
      require_contiguous_array(platform_x1_obj, NPY_FLOAT32, 1, "platform_x1_f32");
  PyArrayObject* platform_y1 =
      require_contiguous_array(platform_y1_obj, NPY_FLOAT32, 1, "platform_y1_f32");
  if (chr == NULL || action == NULL || on_ground == NULL || ground_id == NULL || timer == NULL ||
      pos_x == NULL || pos_y == NULL || platform_ids == NULL || platform_x0 == NULL ||
      platform_y0 == NULL || platform_x1 == NULL || platform_y1 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  const npy_intp width = PyArray_DIM(chr, 1);
  if (require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(on_ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(ground_id, n, width, "ground_id_u16") < 0 ||
      require_exact_2d_shape(timer, n, width, "vanish_travel_timer_u8") < 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x_f32") < 0 ||
      require_exact_2d_shape(pos_y, n, width, "pos_y_f32") < 0) {
    return NULL;
  }
  const npy_intp n_platforms = PyArray_SIZE(platform_ids);
  if (PyArray_SIZE(platform_x0) != n_platforms || PyArray_SIZE(platform_y0) != n_platforms ||
      PyArray_SIZE(platform_x1) != n_platforms || PyArray_SIZE(platform_y1) != n_platforms) {
    PyErr_SetString(PyExc_ValueError, "platform geometry arrays must have equal lengths");
    return NULL;
  }

  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(2, PyArray_DIMS(chr), NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }
  uint16_t* out_skip = (uint16_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < PyArray_SIZE(out); i++) {
    out_skip[i] = 0xFFFFu;
  }
  if (sheik_internal_id < 0 || sheik_internal_id > 255 || travel_frames <= 0 ||
      !(ground_contact_min_frames > 0.0) || PyArray_SIZE(platform_ids) == 0) {
    return (PyObject*)out;
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* floor = (const uint16_t*)PyArray_DATA(ground_id);
  const uint8_t* vanish_timer = (const uint8_t*)PyArray_DATA(timer);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const uint16_t* platforms = (const uint16_t*)PyArray_DATA(platform_ids);
  const float* plat_x0 = (const float*)PyArray_DATA(platform_x0);
  const float* plat_y0 = (const float*)PyArray_DATA(platform_y0);
  const float* plat_x1 = (const float*)PyArray_DATA(platform_x1);
  const float* plat_y1 = (const float*)PyArray_DATA(platform_y1);
  const uint8_t sheik_id = (uint8_t)sheik_internal_id;

  for (npy_intp p = 0; p < width; p++) {
    uint16_t active_skip = 0xFFFFu;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp idx = (i * width) + p;
      const uint16_t action_i = act[idx];
      if (ch[idx] != sheik_id || !msl_py_action_is_teleport_air_start1(ch[idx], action_i) ||
          ground[idx] != 0u) {
        active_skip = 0xFFFFu;
        continue;
      }
      if (active_skip != 0xFFFFu) {
        out_skip[idx] = active_skip;
      }
      const uint16_t line = floor[idx];
      npy_intp platform_i = -1;
      for (npy_intp j = 0; j < n_platforms; j++) {
        if (platforms[j] == line) {
          platform_i = j;
          break;
        }
      }
      if (platform_i < 0) {
        continue;
      }
      const int remaining_after_anim = vanish_timer[idx] > 0u ? (int)vanish_timer[idx] - 1 : 0;
      const int collision_tick = travel_frames - remaining_after_anim;
      const float x0 = plat_x0[platform_i];
      const float y0 = plat_y0[platform_i];
      const float x1 = plat_x1[platform_i];
      const float y1 = plat_y1[platform_i];
      const float denom = x1 - x0;
      float platform_y = y0;
      if (fabsf(denom) > 0.000001f) {
        const float t = (px[idx] - x0) / denom;
        platform_y = y0 + (t * (y1 - y0));
      }
      const uint8_t root_below_platform = py[idx] < platform_y ? 1u : 0u;
      if (root_below_platform && collision_tick >= 0 &&
          (double)collision_tick < ground_contact_min_frames) {
        active_skip = line;
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_camera_target_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOO", &char_obj, &anim_obj, &anim_frame_obj, &scale_y_obj,
                        &facing_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj)) {
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  if (char_id == NULL) {
    return NULL;
  }
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 1, "animation_index_u32");
  if (anim == NULL) {
    return NULL;
  }
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  if (anim_frame == NULL) {
    return NULL;
  }
  PyArrayObject* scale_y =
      require_contiguous_array(scale_y_obj, NPY_FLOAT32, 1, "fighter_scale_y_f32");
  if (scale_y == NULL) {
    return NULL;
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  if (facing == NULL) {
    return NULL;
  }
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  if (pos_x == NULL) {
    return NULL;
  }
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  if (pos_y == NULL) {
    return NULL;
  }
  PyArrayObject* pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 1, "pos_z_f32");
  if (pos_z == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  if (PyArray_DIM(anim, 0) != n || PyArray_DIM(anim_frame, 0) != n ||
      PyArray_DIM(scale_y, 0) != n || PyArray_DIM(facing, 0) != n || PyArray_DIM(pos_x, 0) != n ||
      PyArray_DIM(pos_y, 0) != n || PyArray_DIM(pos_z, 0) != n) {
    PyErr_SetString(PyExc_ValueError, "camera target world derivation inputs must share length");
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "camera target native tables failed to initialize");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_r = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out_x == NULL || out_y == NULL || out_z == NULL || out_r == NULL) {
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    Py_XDECREF(out_r);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = (const float*)PyArray_DATA(pos_z);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);
  float* out_r_p = (float*)PyArray_DATA(out_r);

  for (npy_intp i = 0; i < n; i++) {
    const uint8_t cid = char_p[i];
    const MslCharParams* ch = msl_char_params(cid);
    if (ch == NULL) {
      continue;
    }
    const uint32_t anim_u32 = anim_p[i];
    if (anim_u32 > 0xFFFFu) {
      continue;
    }
    const float frame_f = anim_frame_p[i];
    if (!isfinite(frame_f)) {
      continue;
    }
    const int frame_i = (int)floorf(frame_f);
    if (frame_i < 0 || frame_i > 0xFFFF) {
      continue;
    }
    float m[12];
    if (anim_pose_get_matrix(cid, (uint16_t)anim_u32, (uint16_t)frame_i,
                             ch->camera_zoom_target_bone_part_id, m) != 0) {
      continue;
    }

    const float ox = ch->camera_zoom_target_offset_x;
    const float oy = ch->camera_zoom_target_offset_y;
    const float oz = ch->camera_zoom_target_offset_z;
    float lx = (float)(m[0] * ox + m[1] * oy + m[2] * oz + m[3]);
    float ly = (float)(m[4] * ox + m[5] * oy + m[6] * oz + m[7]);
    float lz = (float)(m[8] * ox + m[9] * oy + m[10] * oz + m[11]);

    float scale = scale_y_p[i];
    if (!isfinite(scale) || !(scale > 0.0f)) {
      scale = 1.0f;
    }
    float model_scaling = ch->model_scaling;
    if (!isfinite(model_scaling) || !(model_scaling > 0.0f)) {
      model_scaling = 1.0f;
    }
    const float pose_scale = (float)(scale * model_scaling);
    lx = (float)(lx * pose_scale);
    ly = (float)(ly * pose_scale);
    lz = (float)(lz * pose_scale);

    const float facing_dir = facing_p[i] ? 1.0f : -1.0f;
    out_x_p[i] = (float)(pos_x_p[i] + facing_dir * lz);
    out_y_p[i] = (float)(pos_y_p[i] + ly);
    out_z_p[i] = (float)(pos_z_p[i] - facing_dir * lx);
    out_r_p[i] = (float)(ch->camera_box_radius * scale);
  }

  return Py_BuildValue("NNNN", out_x, out_y, out_z, out_r);
}

PyObject* msl_derive_hitbox_prev_centers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* rotate_model_obj = NULL;
  PyObject* rotate_valid_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "iOOOOOOOOOOOO", &num_players, &char_obj, &action_obj, &anim_obj,
                        &action_frame_obj, &anim_frame_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj,
                        &facing_obj, &scale_y_obj, &rotate_model_obj, &rotate_valid_obj)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 2, "animation_index");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 2, "action_frame");
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "pos_y");
  PyArrayObject* pos_z = NULL;
  if (pos_z_obj != Py_None) {
    pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 2, "pos_z");
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 2, "facing");
  PyArrayObject* scale_y = require_contiguous_array(scale_y_obj, NPY_FLOAT32, 2, "fighter_scale_y");
  PyArrayObject* rotate_model = NULL;
  if (rotate_model_obj != Py_None) {
    rotate_model =
        require_contiguous_array(rotate_model_obj, NPY_FLOAT32, 2, "specialhi_rotate_model_f32");
  }
  PyArrayObject* rotate_valid = NULL;
  if (rotate_valid_obj != Py_None) {
    rotate_valid =
        require_contiguous_array(rotate_valid_obj, NPY_UINT8, 2, "specialhi_rotate_model_valid_u8");
  }
  if (char_id == NULL || action_id == NULL || anim == NULL || action_frame == NULL ||
      anim_frame == NULL || pos_x == NULL || pos_y == NULL ||
      (pos_z_obj != Py_None && pos_z == NULL) || facing == NULL || scale_y == NULL ||
      (rotate_model_obj != Py_None && rotate_model == NULL) ||
      (rotate_valid_obj != Py_None && rotate_valid == NULL)) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  const npy_intp width = PyArray_DIM(char_id, 1);
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "char_id width smaller than num_players");
    return NULL;
  }
  if (require_exact_2d_shape(action_id, n, width, "action_id") != 0 ||
      require_exact_2d_shape(anim, n, width, "animation_index") != 0 ||
      require_exact_2d_shape(action_frame, n, width, "action_frame") != 0 ||
      require_exact_2d_shape(anim_frame, n, width, "anim_frame_f32") != 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x") != 0 ||
      require_exact_2d_shape(pos_y, n, width, "pos_y") != 0 ||
      (pos_z != NULL && require_exact_2d_shape(pos_z, n, width, "pos_z") != 0) ||
      require_exact_2d_shape(facing, n, width, "facing") != 0 ||
      require_exact_2d_shape(scale_y, n, width, "fighter_scale_y") != 0 ||
      (rotate_model != NULL &&
       require_exact_2d_shape(rotate_model, n, width, "specialhi_rotate_model_f32") != 0) ||
      (rotate_valid != NULL &&
       require_exact_2d_shape(rotate_valid, n, width, "specialhi_rotate_model_valid_u8") != 0)) {
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0 || anim_table_init() != 0 ||
      hitboxes_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "hitbox prev center native tables failed to initialize");
    return NULL;
  }

  npy_intp dims_valid[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  npy_intp dims_xyz[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  PyArrayObject* out_valid = (PyArrayObject*)PyArray_ZEROS(3, dims_valid, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  if (out_valid == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    Py_XDECREF(out_valid);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const int16_t* action_frame_p = (const int16_t*)PyArray_DATA(action_frame);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = pos_z != NULL ? (const float*)PyArray_DATA(pos_z) : NULL;
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const float* rotate_model_p =
      rotate_model != NULL ? (const float*)PyArray_DATA(rotate_model) : NULL;
  const uint8_t* rotate_valid_p =
      rotate_valid != NULL ? (const uint8_t*)PyArray_DATA(rotate_valid) : NULL;
  uint8_t* valid_p = (uint8_t*)PyArray_DATA(out_valid);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);

  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      if (action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      const float af = anim_frame_p[pi];
      if (!isfinite(af) || af < 0.0f) {
        continue;
      }
      const uint16_t frame = (uint16_t)floorf(af);
      const uint8_t cid = char_p[pi];
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }
      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(cid, (uint16_t)anim_p[pi], &events, &event_count) != 0 ||
          events == NULL || event_count == 0) {
        continue;
      }
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

      const float scale_y_val = scale_y_p[pi];
      const float model_scaling =
          (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
      const float model_scale = (float)(scale_y_val * model_scaling);
      const float facing_dir = facing_p[pi] ? 1.0f : -1.0f;
      const float px = pos_x_p[pi];
      const float py = pos_y_p[pi];
      const float pz = pos_z_p != NULL ? pos_z_p[pi] : 0.0f;
      const uint16_t action = action_p[pi];
      const float rotate_model_val = rotate_model_p != NULL ? rotate_model_p[pi] : 0.0f;
      const uint8_t rotate_valid_val = rotate_valid_p != NULL ? rotate_valid_p[pi] : 0u;

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const MslHitboxEvent* ev = active[hb_id];
        if (ev == NULL) {
          continue;
        }
        float m[12];
        if (anim_pose_get_matrix(cid, (uint16_t)anim_p[pi], frame, ev->bone_part_id, m) != 0) {
          continue;
        }
        float lx = 0.0f, ly = 0.0f, lz = 0.0f;
        msl_py_mtx34_mul_point(m, ev->x, ev->y, ev->z, &lx, &ly, &lz);
        lx = (float)(lx * model_scale);
        ly = (float)(ly * model_scale);
        lz = (float)(lz * model_scale);
        (void)msl_py_apply_specialhi_xrotn(cid, action, (uint16_t)anim_p[pi], frame,
                                           ev->bone_part_id, model_scale, rotate_model_val,
                                           rotate_valid_val, &lx, &ly, &lz);
        msl_py_apply_live_transn_tail(cid, (uint16_t)anim_p[pi], frame, ev->bone_part_id,
                                      model_scale, &lx, &ly, &lz);
        const npy_intp oi =
            (fi * (npy_intp)MSL_MAX_PLAYERS + p) * (npy_intp)MSL_MAX_HITBOXES + hb_id;
        valid_p[oi] = 1u;
        out_x_p[oi] = (float)(facing_dir * lz + px);
        out_y_p[oi] = (float)(ly + py);
        out_z_p[oi] = (float)(-facing_dir * lx + pz);
      }
    }
  }

  return Py_BuildValue("NNNN", out_valid, out_x, out_y, out_z);
}

PyObject* msl_derive_combo_push_timer_seed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* combo_count_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* victim_obj = Py_None;
  if (!PyArg_ParseTuple(args, "OO|O", &combo_count_obj, &last_attack_obj, &victim_obj)) {
    return NULL;
  }
  PyArrayObject* counts = require_contiguous_array(combo_count_obj, NPY_UINT8, 2, "combo_count");
  PyArrayObject* attacks =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 2, "last_attack_landed");
  PyArrayObject* victims = NULL;
  if (victim_obj != Py_None) {
    victims = require_contiguous_array(victim_obj, NPY_UINT8, 2, "combo_victim_port");
  }
  if (counts == NULL || attacks == NULL || (victim_obj != Py_None && victims == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(counts, 0);
  const npy_intp count_w = PyArray_DIM(counts, 1);
  if (count_w > (npy_intp)MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "combo_count width exceeds MSL_MAX_PLAYERS");
    return NULL;
  }
  if (require_exact_2d_shape(attacks, n, count_w, "last_attack_landed") != 0 ||
      (victims != NULL && require_exact_2d_shape(victims, n, count_w, "combo_victim_port") != 0)) {
    return NULL;
  }
  if (common_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "common_params_init failed");
    return NULL;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "common params unavailable");
    return NULL;
  }

  npy_intp dims[2] = {n, (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  if (common->combo_push_count_threshold == 0u || common->combo_push_timer_frames == 0u) {
    return (PyObject*)out;
  }

  const uint8_t* counts_p = (const uint8_t*)PyArray_DATA(counts);
  const uint8_t* attacks_p = (const uint8_t*)PyArray_DATA(attacks);
  const uint8_t* victims_p = victims != NULL ? (const uint8_t*)PyArray_DATA(victims) : NULL;
  uint16_t timer[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_count[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_attack[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_victim[MSL_MAX_PLAYERS];
  uint8_t repeated[MSL_MAX_PLAYERS] = {0};
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    prev_victim[i] = 0xFFu;
  }
  const int players = (count_w < (npy_intp)MSL_MAX_PLAYERS) ? (int)count_w : MSL_MAX_PLAYERS;
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < players; p++) {
      const npy_intp pi = fi * count_w + p;
      const uint8_t cur = counts_p[pi];
      const uint8_t attack = attacks_p[pi];
      const uint8_t victim = victims_p != NULL ? victims_p[pi] : 0xFFu;
      const uint8_t same_victim =
          victims_p == NULL || (victim != 0xFFu && victim == prev_victim[p]) ? 1u : 0u;
      const uint8_t same_attack = (attack != 0u && attack == prev_attack[p]) ? 1u : 0u;
      const uint8_t increment = (cur > prev_count[p]) ? 1u : 0u;
      if (cur == 0u || attack == 0u) {
        repeated[p] = 0u;
      } else if (increment) {
        if (same_attack && same_victim) {
          repeated[p] = repeated[p] == 0xFFu ? 0xFFu : (uint8_t)(repeated[p] + 1u);
        } else {
          repeated[p] = 1u;
        }
      } else if (!(same_attack && same_victim)) {
        repeated[p] = 1u;
      }

      if (increment && cur >= common->combo_push_count_threshold &&
          repeated[p] >= common->combo_push_count_threshold) {
        timer[p] = common->combo_push_timer_frames;
      } else if (timer[p] != 0u) {
        timer[p] = (uint16_t)(timer[p] - 1u);
      }
      prev_count[p] = cur;
      prev_attack[p] = attack;
      prev_victim[p] = victim;
    }
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      out_p[fi * (npy_intp)MSL_MAX_PLAYERS + p] = timer[p];
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_throw_pulse_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* rate_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* pulses_obj = NULL;
  PyObject* pulse_count_obj = NULL;
  PyObject* cmd1_obj = NULL;
  PyObject* shot_kind_obj = NULL;
  int num_players = 0;
  int act_throw_b = 0;
  int act_throw_hi = 0;
  int falco_char_id = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOiiii", &action_obj, &char_obj, &anim_obj, &rate_obj,
                        &hitstun_obj, &last_attack_obj, &pulses_obj, &pulse_count_obj, &cmd1_obj,
                        &shot_kind_obj, &num_players, &act_throw_b, &act_throw_hi,
                        &falco_char_id)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* rate = require_contiguous_array(rate_obj, NPY_FLOAT32, 2, "frame_speed_mul_f32");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 2, "hitstun_u16");
  PyArrayObject* last_attack =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 2, "last_attack_landed_u8");
  PyArrayObject* pulses = require_contiguous_array(pulses_obj, NPY_INT16, 3, "pulse_lut");
  PyArrayObject* pulse_count =
      require_contiguous_array(pulse_count_obj, NPY_UINT8, 2, "pulse_count_lut");
  PyArrayObject* cmd1 = require_contiguous_array(cmd1_obj, NPY_INT16, 2, "cmd1_start_lut");
  PyArrayObject* shot_kind =
      require_contiguous_array(shot_kind_obj, NPY_UINT16, 1, "shot_itkind_lut");
  if (action == NULL || chr == NULL || anim == NULL || rate == NULL || hitstun == NULL ||
      last_attack == NULL || pulses == NULL || pulse_count == NULL || cmd1 == NULL ||
      shot_kind == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(anim, n, width, "anim_frame_f32") < 0 ||
      require_exact_2d_shape(rate, n, width, "frame_speed_mul_f32") < 0 ||
      require_exact_2d_shape(hitstun, n, width, "hitstun_u16") < 0 ||
      require_exact_2d_shape(last_attack, n, width, "last_attack_landed_u8") < 0) {
    return NULL;
  }
  if (PyArray_DIM(pulses, 0) < 256 || PyArray_DIM(pulse_count, 0) < 256 ||
      PyArray_DIM(cmd1, 0) < 256 || PyArray_DIM(shot_kind, 0) < 256 ||
      PyArray_DIM(pulses, 1) != PyArray_DIM(pulse_count, 1) ||
      PyArray_DIM(pulses, 1) != PyArray_DIM(cmd1, 1)) {
    PyErr_SetString(PyExc_ValueError, "throw pulse LUTs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for throw pulse width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_consumed = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_crossed = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_pending = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  if (out_consumed == NULL || out_crossed == NULL || out_pending == NULL) {
    Py_XDECREF(out_consumed);
    Py_XDECREF(out_crossed);
    Py_XDECREF(out_pending);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const float* af = (const float*)PyArray_DATA(anim);
  const float* r = (const float*)PyArray_DATA(rate);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* lal = (const uint8_t*)PyArray_DATA(last_attack);
  const int16_t* pulse_lut = (const int16_t*)PyArray_DATA(pulses);
  const uint8_t* count_lut = (const uint8_t*)PyArray_DATA(pulse_count);
  const int16_t* cmd1_lut = (const int16_t*)PyArray_DATA(cmd1);
  const uint16_t* shot_lut = (const uint16_t*)PyArray_DATA(shot_kind);
  uint8_t* consumed = (uint8_t*)PyArray_DATA(out_consumed);
  uint8_t* crossed = (uint8_t*)PyArray_DATA(out_crossed);
  uint8_t* pending = (uint8_t*)PyArray_DATA(out_pending);
  const npy_intp action_cap = PyArray_DIM(pulses, 1);
  const npy_intp max_pulses = PyArray_DIM(pulses, 2);
  int cursor_char[4] = {-1, -1, -1, -1};
  int cursor_action[4] = {-1, -1, -1, -1};
  int cursor_next_idx[4] = {0, 0, 0, 0};
  float cursor_timer[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool cursor_active[4] = {false, false, false, false};
  for (npy_intp i = 0; i < n; i++) {
    for (int p = 0; p < players; p++) {
      const npy_intp idx = (i * width) + p;
      const uint16_t action_id = a[idx];
      const uint8_t char_id = c[idx];
      uint8_t count = 0u;
      const int16_t* pulse_base = NULL;
      if ((npy_intp)action_id < action_cap) {
        count = count_lut[((npy_intp)char_id * action_cap) + action_id];
        if (count > max_pulses) count = (uint8_t)max_pulses;
        pulse_base = &pulse_lut[(((npy_intp)char_id * action_cap) + action_id) * max_pulses];
      }
      if (count == 0u || pulse_base == NULL) {
        cursor_active[p] = false;
        cursor_char[p] = char_id;
        cursor_action[p] = action_id;
        continue;
      }
      const float cur_af = af[idx];
      const float cur_rate = r[idx];
      if (!isfinite(cur_af) || !isfinite(cur_rate) || cur_rate <= 0.0f) {
        cursor_active[p] = false;
        continue;
      }
      if (!cursor_active[p] || cursor_char[p] != (int)char_id ||
          cursor_action[p] != (int)action_id || (i > 0 && af[((i - 1) * width) + p] > cur_af)) {
        cursor_char[p] = char_id;
        cursor_action[p] = action_id;
        cursor_active[p] = true;
        int next_idx = 0;
        while (next_idx < count && (float)pulse_base[next_idx] <= cur_af) next_idx++;
        cursor_next_idx[p] = next_idx;
        cursor_timer[p] =
            next_idx < count ? fmaxf((float)pulse_base[next_idx] - cur_af, 0.0f) : INFINITY;
      }
      if (cursor_active[p] && cursor_next_idx[p] < count) {
        const float timer_after = cursor_timer[p] - cur_rate;
        if (timer_after <= 1.0e-4f) {
          const int pulse = pulse_base[cursor_next_idx[p]];
          if (pulse > 0 && pulse <= 255) pending[(i * 4) + p] = (uint8_t)pulse;
          cursor_next_idx[p]++;
          cursor_timer[p] = cursor_next_idx[p] < count
                                ? (float)(pulse_base[cursor_next_idx[p]] - pulse)
                                : INFINITY;
        } else {
          cursor_timer[p] = timer_after;
        }
      }
      const float prev_af = cur_af - cur_rate;
      int crossed_pulse = -1;
      for (int k = 0; k < count; k++) {
        const float pf = (float)pulse_base[k];
        if (prev_af < pf && pf <= cur_af) {
          crossed_pulse = (int)pulse_base[k];
          break;
        }
      }
      if (crossed_pulse < 0) continue;
      if (crossed_pulse > 0 && crossed_pulse <= 255) crossed[(i * 4) + p] = (uint8_t)crossed_pulse;
      const int prev_frame_i = (int)floorf(prev_af);
      bool stale_window = false;
      if ((int)action_id == act_throw_b && count >= 1u) {
        int cmd1_start = -1;
        if ((npy_intp)action_id < action_cap)
          cmd1_start = (int)cmd1_lut[((npy_intp)char_id * action_cap) + action_id];
        if (cmd1_start >= 0 && crossed_pulse == (int)pulse_base[0] && prev_frame_i == cmd1_start) {
          stale_window = true;
        }
      } else if ((int)action_id == act_throw_hi && (int)char_id == falco_char_id && count >= 2u) {
        if (crossed_pulse == (int)pulse_base[1] && prev_frame_i == (int)pulse_base[0]) {
          stale_window = true;
        }
      }
      if (!stale_window && (int)action_id == act_throw_b) {
        const int shot_itkind = (int)shot_lut[char_id];
        if (shot_itkind != 0) {
          for (int vp = 0; vp < players; vp++) {
            if (vp == p) continue;
            const npy_intp v_idx = (i * width) + vp;
            if (hs[v_idx] > 0u && (int)lal[v_idx] == shot_itkind) {
              stale_window = true;
              break;
            }
          }
        }
      }
      if (stale_window) consumed[(i * 4) + p] = 1u;
    }
  }
  return Py_BuildValue("NNN", out_consumed, out_crossed, out_pending);
}

static inline bool msl_py_hidden_z_action_allows_depth(uint16_t a) {
  if (msl_py_action_is_airborne_damage_family(a)) return false;
  if (a == 0x00F7u || a == 0x00F8u) return false;
  if (a == 0x00B5u || a == 0x00B7u || a == 0x00BFu || a == 0x00FCu || a == 0x00FDu) {
    return false;
  }
  if (a >= 0x00DBu && a <= 0x00E2u) return false;
  if (a >= 0x012Cu) return false;
  return true;
}

PyObject* msl_derive_grounded_overlap_hidden_pos_z_py(PyObject* self, PyObject* args) {
  (void)self;
  int num_players = 0;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* push_x_obj = NULL;
  PyObject* push_y_obj = NULL;
  double step_d = 0.0;
  double z_max_d = 0.0;
  if (!PyArg_ParseTuple(args, "iOOOOOOOOOdd", &num_players, &char_obj, &action_obj, &ground_obj,
                        &stocks_obj, &pos_x_obj, &pos_z_obj, &facing_obj, &push_x_obj, &push_y_obj,
                        &step_d, &z_max_d)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* stocks = require_contiguous_array(stocks_obj, NPY_UINT8, 2, "stocks_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 2, "pos_z_f32");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 2, "facing_u8");
  PyArrayObject* push_x = require_contiguous_array(push_x_obj, NPY_FLOAT32, 1, "push_x_lut");
  PyArrayObject* push_y = require_contiguous_array(push_y_obj, NPY_FLOAT32, 1, "push_y_lut");
  if (chr == NULL || action == NULL || ground == NULL || stocks == NULL || pos_x == NULL ||
      pos_z == NULL || facing == NULL || push_x == NULL || push_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  const npy_intp width = PyArray_DIM(chr, 1);
  if (num_players < 0 || num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for hidden pos_z shape");
    return NULL;
  }
  if (require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(stocks, n, width, "stocks_u8") < 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x_f32") < 0 ||
      require_exact_2d_shape(pos_z, n, width, "pos_z_f32") < 0 ||
      require_exact_2d_shape(facing, n, width, "facing_u8") < 0) {
    return NULL;
  }
  if (PyArray_SIZE(push_x) < 256 || PyArray_SIZE(push_y) < 256) {
    PyErr_SetString(PyExc_ValueError, "pushbox LUTs must have at least 256 entries");
    return NULL;
  }
  PyArrayObject* out = (PyArrayObject*)PyArray_NewCopy(pos_z, NPY_CORDER);
  if (out == NULL) return NULL;
  const float step = (float)step_d;
  const float z_max = (float)z_max_d;
  if (!(step > 0.0f && z_max > 0.0f)) {
    return (PyObject*)out;
  }
  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* gr = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* st = (const uint8_t*)PyArray_DATA(stocks);
  const float* x = (const float*)PyArray_DATA(pos_x);
  const uint8_t* fac = (const uint8_t*)PyArray_DATA(facing);
  const float* px = (const float*)PyArray_DATA(push_x);
  const float* py = (const float*)PyArray_DATA(push_y);
  float* out_p = (float*)PyArray_DATA(out);
  float z_step[4] = {0};
  for (npy_intp fi = 1; fi < n; fi++) {
    for (npy_intp p = 0; p < width; p++) {
      out_p[(fi * width) + p] = ((const float*)PyArray_DATA(pos_z))[(fi * width) + p];
    }
    for (int k = 0; k < 4; k++) z_step[k] = 0.0f;
    for (int p = 0; p < num_players; p++) {
      const npy_intp idx_prev_p = ((fi - 1) * width) + p;
      const npy_intp idx_cur_p = (fi * width) + p;
      const uint8_t cid = ch[idx_prev_p];
      if (st[idx_cur_p] == 0u || gr[idx_cur_p] == 0u || st[idx_prev_p] == 0u ||
          gr[idx_prev_p] == 0u || !msl_py_hidden_z_action_allows_depth(act[idx_cur_p]) ||
          !msl_py_hidden_z_action_allows_depth(act[idx_prev_p]) || !(py[cid] > 0.0f)) {
        continue;
      }
      const float p_push = py[cid];
      const float p_face = fac[idx_prev_p] != 0u ? 1.0f : -1.0f;
      const float p_center = x[idx_prev_p] + px[cid] * p_face;
      for (int q = 0; q < num_players; q++) {
        if (q == p) continue;
        const npy_intp idx_prev_q = ((fi - 1) * width) + q;
        const npy_intp idx_cur_q = (fi * width) + q;
        const uint8_t qid = ch[idx_prev_q];
        if (st[idx_cur_q] == 0u || gr[idx_cur_q] == 0u || st[idx_prev_q] == 0u ||
            gr[idx_prev_q] == 0u || !msl_py_hidden_z_action_allows_depth(act[idx_cur_q]) ||
            !msl_py_hidden_z_action_allows_depth(act[idx_prev_q]) || !(py[qid] > 0.0f)) {
          continue;
        }
        const float q_face = fac[idx_prev_q] != 0u ? 1.0f : -1.0f;
        const float q_center = x[idx_prev_q] + px[qid] * q_face;
        const float delta_x = p_center - q_center;
        if (fabsf(delta_x) >= p_push + py[qid]) continue;
        const float delta_z = out_p[idx_prev_p] - out_p[idx_prev_q];
        if (delta_z < 0.0f) {
          z_step[p] -= step;
        } else if (delta_z > 0.0f) {
          z_step[p] += step;
        } else if (delta_x < 0.0f) {
          z_step[p] -= step;
        } else if (delta_x > 0.0f) {
          z_step[p] += step;
        } else if (q < p) {
          z_step[p] -= step;
        } else {
          z_step[p] += step;
        }
      }
    }
    for (int p = 0; p < num_players; p++) {
      const npy_intp idx_prev = ((fi - 1) * width) + p;
      const npy_intp idx_cur = (fi * width) + p;
      const uint8_t cid = ch[idx_prev];
      if (st[idx_cur] == 0u || gr[idx_cur] == 0u || st[idx_prev] == 0u || gr[idx_prev] == 0u ||
          !msl_py_hidden_z_action_allows_depth(act[idx_cur]) ||
          !msl_py_hidden_z_action_allows_depth(act[idx_prev]) || !(py[cid] > 0.0f)) {
        continue;
      }
      const float z = out_p[idx_prev];
      float dz = z_step[p];
      if (dz == 0.0f && z != 0.0f) dz = z < 0.0f ? step : -step;
      if ((dz > 0.0f && z < 0.0f && z + dz >= 0.0f) || (dz < 0.0f && z > 0.0f && z + dz <= 0.0f)) {
        dz = -z;
      }
      if (z + dz > z_max) {
        dz = z_max - z;
      } else if (z + dz < -z_max) {
        dz = -z_max - z;
      }
      out_p[idx_cur] = z + dz;
    }
  }
  return (PyObject*)out;
}
