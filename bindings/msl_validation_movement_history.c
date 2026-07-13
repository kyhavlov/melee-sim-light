/* Native validation derivation for movement, ECB, ledge, and action history lanes. */

#include "msl_validation_movement_history.h"
#include "msl_validation_history_common.h"

#include "../src/api.h"
#include "../src/action_ids.h"
#include "../src/escapeair_collision_owner.h"
#include "../src/ids.h"
#include "../src/input_axis.h"
#include "../src/move_tables.h"
#include "../src/motion_state_owners.h"
#include "../src/mpcoll_ecb_points.h"

PyObject* msl_derive_guard_setoff_post_hitlag_owner_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* phase_obj = NULL;
  PyObject* flags_obj = NULL;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOi", &action_obj, &phase_obj, &flags_obj, &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* phase =
      require_contiguous_array(phase_obj, NPY_UINT8, 1, "guard_setoff_hitlag_exit_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 1, "state_flags_221c_u8");
  if (action == NULL || phase == NULL || flags == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(phase) != n || PyArray_SIZE(flags) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id/phase/state_flags_221c must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* ph = (const uint8_t*)PyArray_DATA(phase);
  const uint8_t* fl = (const uint8_t*)PyArray_DATA(flags);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_guard_set_off) continue;
    if (ph[i] != 2u && ph[i] != 3u) continue;
    out_p[i] = (uint8_t)((fl[i] & 0x20u) != 0u ? 2u : 1u);
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_setoff_exit_frame_speed_seed_lane_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOii", &action_obj, &hitlag_obj, &frame_speed_obj, &num_players,
                        &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 2, "frame_speed_mul_f32");
  if (action == NULL || hitlag == NULL || frame_speed == NULL) return NULL;
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (PyArray_DIM(hitlag, 0) != n || PyArray_DIM(frame_speed, 0) != n ||
      PyArray_DIM(hitlag, 1) != width || PyArray_DIM(frame_speed, 1) != width) {
    PyErr_SetString(PyExc_ValueError, "GuardSetOff exit-rate inputs must have the same shape");
    return NULL;
  }
  if (num_players < 0 || num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players is outside the input width");
    return NULL;
  }
  npy_intp dims[2] = {n, width};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;

  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const float* rate = (const float*)PyArray_DATA(frame_speed);
  float* out_p = (float*)PyArray_DATA(out);
  for (int p = 0; p < num_players; p++) {
    npy_intp i = 0;
    while (i < n) {
      const npy_intp idx = i * width + p;
      if ((int)a[idx] != act_guard_set_off || hl[idx] == 0u) {
        i++;
        continue;
      }

      const npy_intp start = i;
      while (i < n && (int)a[i * width + p] == act_guard_set_off && hl[i * width + p] > 0u) {
        i++;
      }
      if (i >= n || (int)a[i * width + p] != act_guard_set_off || hl[i * width + p] != 0u) {
        continue;
      }

      const float exit_rate = rate[i * width + p];
      if (!(isfinite(exit_rate) && exit_rate > 0.0f)) {
        continue;
      }

      // GuardSetOff hitlag hides the `fp->frame_speed_mul` value that source consumes when
      // Fighter_8006A1BC exits hitlag and Fighter_8006A360 advances animation. The replay-visible
      // first non-hitlag GuardSetOff row publishes that rate; seed it across the frozen segment,
      // plus the immediately preceding shield-hit entry row, so rollouts that start before or
      // inside the segment carry the same source-owned value until exit.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80092F2C,ftCo_GuardSetOff_Anim}
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
      for (npy_intp j = start; j < i; j++) {
        out_p[j * width + p] = exit_rate;
      }
      if (start > 0 && (int)a[(start - 1) * width + p] != act_guard_set_off) {
        out_p[(start - 1) * width + p] = exit_rate;
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_run_x0_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  double run_x0_init = 0.0;
  int act_run = 0;
  int act_run_direct = 0;
  int act_turn_run = 0;
  if (!PyArg_ParseTuple(args, "OOdiii", &action_obj, &hitlag_obj, &run_x0_init, &act_run,
                        &act_run_direct, &act_turn_run)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id and hitlag_u16 must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int init = (int)run_x0_init;
  if (init < 0) init = 0;
  if (init > 255) init = 255;
  for (npy_intp i = 1; i < n; i++) {
    const int prev_a = (int)a[i - 1];
    const int cur_a = (int)a[i];
    const int prev_x0 = (int)out_p[i - 1];
    int x0 = 0;
    if (cur_a == act_run || cur_a == act_run_direct) {
      if (prev_a == act_turn_run) {
        x0 = init;
      } else if (prev_a == cur_a) {
        x0 = prev_x0;
        if (hl[i - 1] == 0u && x0 > 0) x0 -= 1;
      }
    }
    out_p[i] = (uint8_t)x0;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_runbrake_cmd0_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* on_obj = NULL;
  PyObject* off_obj = NULL;
  int act_run_brake = 0;
  if (!PyArg_ParseTuple(args, "OOOOOi", &action_obj, &anim_obj, &char_obj, &on_obj, &off_obj,
                        &act_run_brake)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  if (action == NULL || anim == NULL || chr == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(anim) != n || PyArray_SIZE(chr) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id_u16/anim_frame_f32/char_id_u8 must match length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const float* af = (const float*)PyArray_DATA(anim);
  const uint8_t* cid = (const uint8_t*)PyArray_DATA(chr);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_run_brake) continue;
    PyObject* key = PyLong_FromLong((long)cid[i]);
    if (key == NULL) return NULL;
    PyObject* on_val = PyObject_GetItem(on_obj, key);
    PyObject* off_val = PyObject_GetItem(off_obj, key);
    Py_DECREF(key);
    if (on_val == NULL || off_val == NULL) {
      PyErr_Clear();
      Py_XDECREF(on_val);
      Py_XDECREF(off_val);
      continue;
    }
    const long start = PyLong_AsLong(on_val);
    const long end = PyLong_AsLong(off_val);
    Py_DECREF(on_val);
    Py_DECREF(off_val);
    if (PyErr_Occurred()) return NULL;
    if (start < 0 || end < 0 || end < start) continue;
    if (isfinite(af[i]) && af[i] >= (float)start && af[i] < (float)end) {
      out_p[i] = 1u;
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_dash_x4_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  int act_dash = 0;
  int act_turn = 0;
  if (!PyArg_ParseTuple(args, "OOii", &action_obj, &frame_obj, &act_dash, &act_turn)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (action == NULL || frame == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n) {
    PyErr_SetString(PyExc_ValueError, "action_frame_i16 must match action_id_u16 length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  const uint16_t dash = (uint16_t)((uint32_t)act_dash & 0xFFFFu);
  const uint16_t turn = (uint16_t)((uint32_t)act_turn & 0xFFFFu);
  uint8_t x4 = 0u;
  for (npy_intp i = 0; i < n; i++) {
    if (a[i] != dash) {
      x4 = 0u;
      continue;
    }
    const uint16_t prev_a = i > 0 ? a[i - 1] : a[i];
    const int16_t prev_af = i > 0 ? af[i - 1] : af[i];
    const bool dash_entry = i == 0 || a[i] != prev_a || af[i] < prev_af;
    if (dash_entry) {
      x4 = (uint8_t)(prev_a == turn ? 0u : 1u);
    }
    out_p[i] = x4;
  }
  return (PyObject*)out;
}

typedef struct ValidationEcbLockRefresh {
  uint8_t timer;
  uint8_t owner;
} ValidationEcbLockRefresh;

static inline ValidationEcbLockRefresh validation_ecb_lock_refresh(uint8_t timer, uint8_t owner) {
  const ValidationEcbLockRefresh refresh = {timer, owner};
  return refresh;
}

static inline uint8_t validation_falcon_ground_special_floor_loss_proven(uint16_t prev_action,
                                                                         uint16_t action) {
  // These action pairs are direct outputs of collision callbacks that call ftCommon_8007D5D4.
  // Same-action rows are required where the helper changes only ground_or_air; Falcon Punch also
  // changes to its authored aerial motion state. Damage* output is not floor-loss proof.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialN_Coll
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHi_Coll
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
  //   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll}
  if (prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_N) {
    return action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_N ? 1u : 0u;
  }
  if (prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END ||
      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR) {
    return action == prev_action ? 1u : 0u;
  }
  return 0u;
}

static inline uint8_t validation_falcon_ground_special_damage_output(uint16_t prev_action,
                                                                     uint16_t action) {
  const uint8_t grounded_special =
      (uint8_t)(prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_N ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_S_START ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_S ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END ||
                prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR);
  const uint8_t damage_output =
      (uint8_t)(action == (uint16_t)MSL_ACT_DAMAGE_FALL || msl_py_damage_action_any(action));
  return (uint8_t)(grounded_special && damage_output);
}

static inline uint8_t validation_common_post_map_ground_to_air(uint16_t action) {
  // These visible destinations can only acquire the ordinary ten-frame ECB lock after the
  // current frame's Fighter_procMap decrement:
  // - a grounded common collision callback loses its floor and enters Fall;
  // - Fighter_ProcessHit runs after procMap and enters the Damage family.
  // Jump and character-special entry owners run before procMap and retain the nine-frame generic
  // post value below. Character-special collision owners with exact source proof are handled by
  // validation_falcon_ecb_lock_post_refresh.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
  return (uint8_t)(action == (uint16_t)MSL_ACT_FALL || action == (uint16_t)MSL_ACT_DAMAGE_FALL ||
                   msl_py_damage_action_any(action));
}

static ValidationEcbLockRefresh validation_falcon_ecb_lock_post_refresh(
    uint8_t char_id, uint16_t action, int16_t action_frame, uint16_t prev_action,
    uint8_t prev_ground, uint8_t action_entry) {
  const uint8_t no_owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
  const uint8_t seeded_owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130;
  const uint8_t live_owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_FTCOMMON;
  if (char_id != (uint8_t)MSL_CHAR_ID_FALCON) {
    return validation_ecb_lock_refresh(0xFFu, no_owner);
  }

  // Anim/IASA callbacks run before Fighter_procMap, so 10/5-frame locks publish as 9/4 after the
  // same-frame decrement. Collision callbacks run inside Fighter_procMap after that decrement and
  // publish the full 10/5. These bounded Falcon owners cannot be recovered from a generic visible
  // ground->air edge alone.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
  //   ftCa_SpecialAirS_Enter,ftCa_SpecialAirSStart_Anim,ftCa_SpecialAirS_Anim,
  //   ftCa_SpecialSStart_Coll,ftCa_SpecialS_Coll}
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Anim
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
  //   ftCa_SpecialLw_Anim_inline,ftCa_SpecialAirLw_Anim,ftCa_SpecialLw_Coll}
  if (action == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
    if (!action_entry || action_frame > 0) {
      return validation_ecb_lock_refresh(4u, live_owner);
    }
    // Dive release can run ftCommon_8007D5D4 before entering Throw0; its first visible row has not
    // yet run Throw0_Anim's recurring five-frame refresh.
    return validation_ecb_lock_refresh(9u, live_owner);
  }
  if (prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW) {
    // Throw0_Anim refreshes the five-frame lock before either anim-end -> Fall or a later
    // Fighter_ProcessHit interruption can replace the visible action.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Anim
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
    return validation_ecb_lock_refresh(4u, live_owner);
  }
  if (action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START && action_entry) {
    return validation_ecb_lock_refresh(4u, live_owner);
  }
  if ((prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START ||
       prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S) &&
      (action == (uint16_t)MSL_ACT_FALL || action == (uint16_t)MSL_ACT_FALL_SPECIAL ||
       action == (uint16_t)MSL_ACT_FALL_SPECIAL_F || action == (uint16_t)MSL_ACT_FALL_SPECIAL_B)) {
    return validation_ecb_lock_refresh(4u, live_owner);
  }
  const uint8_t raptor_ground_floor_loss_output =
      (uint8_t)(action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S_START ||
                action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_S || action == (uint16_t)MSL_ACT_FALL ||
                action == (uint16_t)MSL_ACT_FALL_SPECIAL ||
                action == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
                action == (uint16_t)MSL_ACT_FALL_SPECIAL_B);
  if (prev_ground && raptor_ground_floor_loss_output &&
      (prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_S_START ||
       prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_S)) {
    // Only the Raptor Coll callback's floor-loss outputs own ftCommon_8007D60C. Incoming damage
    // can also follow a grounded Raptor row, but its ordinary ground-to-air bundle owns the
    // ten-frame lock instead.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
    //   ftCa_SpecialSStart_Coll,ftCa_SpecialS_Coll}
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C}
    return validation_ecb_lock_refresh(5u, live_owner);
  }
  if (action_entry && (action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END_AIR ||
                       action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR)) {
    return validation_ecb_lock_refresh(9u, live_owner);
  }
  if (action_entry && action == (uint16_t)MSL_ACT_CA_SPECIAL_HI_THROW1) {
    return validation_ecb_lock_refresh(10u, live_owner);
  }
  if (prev_ground && (prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_N ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END ||
                      prev_action == (uint16_t)MSL_ACT_CA_SPECIAL_LW_END_AIR)) {
    // Grounded special -> airborne Damage* still carries a ten-frame lock because incoming
    // ProcessHit runs after procMap, but that timer does not prove the special Coll callback took
    // its floor-loss branch. Only the exact source-backed pairs above promote owner 4.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    const uint8_t owner = validation_falcon_ground_special_floor_loss_proven(prev_action, action)
                              ? live_owner
                              : seeded_owner;
    return validation_ecb_lock_refresh(10u, owner);
  }
  return validation_ecb_lock_refresh(0xFFu, no_owner);
}

PyObject* msl_derive_ecb_lock_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* ground_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_frame_obj = NULL;
  int lock_frames = 10;
  int act_jump_f = 0;
  int act_jump_b = 0;
  int act_jump_aerial_f = 0;
  int act_jump_aerial_b = 0;
  if (!PyArg_ParseTuple(args, "OOOOiiiii", &ground_obj, &action_obj, &char_obj, &action_frame_obj,
                        &lock_frames, &act_jump_f, &act_jump_b, &act_jump_aerial_f,
                        &act_jump_aerial_b)) {
    return NULL;
  }
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* char_arr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (ground == NULL || action == NULL || char_arr == NULL || action_frame == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(ground);
  if (PyArray_SIZE(action) != n || PyArray_SIZE(char_arr) != n || PyArray_SIZE(action_frame) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "action_id_u16/char_id_u8/action_frame_i16 must match on_ground_u8 length");
    return NULL;
  }
  if (lock_frames < 0) lock_frames = 0;
  if (lock_frames > 255) lock_frames = 255;
  const int set_post = lock_frames > 0 ? lock_frames - 1 : 0;
  npy_intp dims[1] = {n};
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_owner = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_timer == NULL || out_owner == NULL) {
    Py_XDECREF(out_timer);
    Py_XDECREF(out_owner);
    return NULL;
  }
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_arr);
  const int16_t* af = (const int16_t*)PyArray_DATA(action_frame);
  uint8_t* timer_p = (uint8_t*)PyArray_DATA(out_timer);
  uint8_t* owner_p = (uint8_t*)PyArray_DATA(out_owner);
  int timer = 0;
  uint8_t owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
  bool prev_ground = n > 0 && g[0] != 0u;
  for (npy_intp i = 0; i < n; i++) {
    const bool cur_ground = g[i] != 0u;
    const int cur_action = (int)a[i];
    const int prev_action = i > 0 ? (int)a[i - 1] : cur_action;
    const int16_t prev_af = i > 0 ? af[i - 1] : af[i];
    const uint8_t action_entry =
        (uint8_t)(i > 0 ? (cur_action != prev_action || af[i] < prev_af) : (af[i] <= 0));
    const bool is_jump = cur_action == act_jump_f || cur_action == act_jump_b ||
                         cur_action == act_jump_aerial_f || cur_action == act_jump_aerial_b;
    const bool jump_entry = i > 0 && is_jump && cur_action != prev_action;
    const ValidationEcbLockRefresh falcon_refresh = validation_falcon_ecb_lock_post_refresh(
        char_p[i], a[i], af[i], (uint16_t)prev_action, prev_ground ? 1u : 0u, action_entry);
    if (cur_ground) {
      timer = 0;
      owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
    } else if (falcon_refresh.timer != 0xFFu) {
      timer = falcon_refresh.timer;
      owner = falcon_refresh.owner;
    } else if (jump_entry ||
               (i > 0 && prev_ground && !msl_action_is_thrown_victim((uint16_t)prev_action))) {
      // Thrown* has empty Phys/Coll callbacks and keeps its attached accessory owner through the
      // pre-release window. A replay-visible Thrown* -> airborne Damage transition therefore does
      // not prove ftCommon_8007D5D4's grounded launch/ECB-lock call; synthesizing that lock pins
      // the new Damage ECB bottom to the floor and creates a false DownDamage contact.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
      //   ftCo_ThrownF_Phys,ftCo_ThrownF_Coll,ftCo_ThrownLw_Phys,ftCo_ThrownLw_Coll}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      timer = i > 0 && prev_ground && !jump_entry && validation_common_post_map_ground_to_air(a[i])
                  ? lock_frames
                  : set_post;
      owner = timer != 0 ? (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130
                         : (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
    } else if (timer > 0) {
      timer -= 1;
      if (timer == 0) {
        owner = (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
      }
    }
    timer_p[i] = (uint8_t)timer;
    owner_p[i] = owner;
    prev_ground = cur_ground;
  }
  return Py_BuildValue("NN", (PyObject*)out_timer, (PyObject*)out_owner);
}

PyObject* msl_derive_ecb_lock_bottom_rel_y_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* lock_obj = NULL;
  PyObject* lock_owner_obj = NULL;
  int act_jump_aerial_f = MSL_ACT_JUMP_AERIAL_F;
  int act_jump_aerial_b = MSL_ACT_JUMP_AERIAL_B;
  if (!PyArg_ParseTuple(args, "OOOOOOO|ii", &char_obj, &action_obj, &anim_obj, &anim_frame_obj,
                        &ground_obj, &lock_obj, &lock_owner_obj, &act_jump_aerial_f,
                        &act_jump_aerial_b)) {
    return NULL;
  }
  PyArrayObject* char_arr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action_arr = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* anim_arr =
      require_contiguous_array(anim_obj, NPY_UINT32, 1, "animation_index_u32");
  PyArrayObject* anim_frame_arr =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* ground_arr = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* lock_arr = require_contiguous_array(lock_obj, NPY_UINT8, 1, "ecb_lock_timer_u8");
  PyArrayObject* lock_owner_arr =
      require_contiguous_array(lock_owner_obj, NPY_UINT8, 1, "ecb_lock_owner_u8");
  if (char_arr == NULL || action_arr == NULL || anim_arr == NULL || anim_frame_arr == NULL ||
      ground_arr == NULL || lock_arr == NULL || lock_owner_arr == NULL) {
    return NULL;
  }
  if (ecb_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "ecb_table_init failed");
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(char_arr);
  if (PyArray_SIZE(action_arr) != n || PyArray_SIZE(anim_arr) != n ||
      PyArray_SIZE(anim_frame_arr) != n || PyArray_SIZE(ground_arr) != n ||
      PyArray_SIZE(lock_arr) != n || PyArray_SIZE(lock_owner_arr) != n) {
    PyErr_SetString(PyExc_ValueError, "ECB lock-bottom inputs must have matching length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* bottom_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* owner_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (bottom_arr == NULL || owner_arr == NULL) {
    Py_XDECREF(bottom_arr);
    Py_XDECREF(owner_arr);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_arr);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_arr);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim_arr);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame_arr);
  const uint8_t* ground_p = (const uint8_t*)PyArray_DATA(ground_arr);
  const uint8_t* lock_p = (const uint8_t*)PyArray_DATA(lock_arr);
  const uint8_t* lock_owner_p = (const uint8_t*)PyArray_DATA(lock_owner_arr);
  float* bottom_p = (float*)PyArray_DATA(bottom_arr);
  uint8_t* owner_p = (uint8_t*)PyArray_DATA(owner_arr);

  float desired_bottom = 0.0f;
  uint8_t desired_valid = 0u;
  uint8_t episode_preserves_desired_bottom = 0u;
  uint8_t prev_lock = 0u;
  const uint16_t jaf = (uint16_t)((uint32_t)act_jump_aerial_f & 0xFFFFu);
  const uint16_t jab = (uint16_t)((uint32_t)act_jump_aerial_b & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const uint8_t char_id = char_p[i];
    const uint32_t anim = anim_p[i];
    const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(anim_frame_p[i]);
    const float pose_bottom = msl_ecb_bottom_rel_y(char_id, anim, (int)frame);
    if (ground_p[i] != 0u) {
      desired_bottom = 0.0f;
      desired_valid = 1u;
      episode_preserves_desired_bottom = 0u;
      prev_lock = lock_p[i];
      continue;
    }
    if (lock_p[i] != 0u) {
      const uint8_t lock_start = (i == 0 || prev_lock == 0u || lock_p[i] > prev_lock) ? 1u : 0u;
      if (lock_start) {
        const uint16_t action = action_p[i];
        const uint16_t prev_action = i > 0 ? action_p[i - 1] : action;
        const uint8_t lock_owner = msl_escapeair_locked_bottom_owner_normalize(lock_owner_p[i]);
        const uint8_t falcon_special_damage_owner =
            (uint8_t)(i > 0 && ground_p[i - 1] != 0u && char_id == (uint8_t)MSL_CHAR_ID_FALCON &&
                      lock_owner == (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130 &&
                      validation_falcon_ground_special_damage_output(prev_action, action));
        // Both common lock helpers preserve the existing desired bottom. derive_ecb_lock_state
        // publishes an owner only for a prefix-causal lock episode (jump entry, grounded-to-air
        // handoff, or the bounded Falcon refresh); the countdown alone is never provenance.
        // Carry every such owned episode instead of discarding the ordinary ground-to-air owner.
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C}
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        episode_preserves_desired_bottom =
            (uint8_t)(action == jaf || action == jab ||
                      lock_owner != (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE ||
                      falcon_special_damage_owner);
      }
      if (!desired_valid) {
        desired_bottom = pose_bottom;
        desired_valid = 1u;
      }
      if (episode_preserves_desired_bottom) {
        bottom_p[i] = desired_bottom;
        owner_p[i] = msl_escapeair_locked_bottom_owner_normalize(lock_owner_p[i]);
      }
      prev_lock = lock_p[i];
      continue;
    }
    desired_bottom = pose_bottom;
    desired_valid = 1u;
    episode_preserves_desired_bottom = 0u;
    prev_lock = 0u;
  }

  PyObject* ret = Py_BuildValue("(NN)", bottom_arr, owner_arr);
  return ret;
}

PyObject* msl_derive_damage_hitlag_colldata_ecb_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOO", &char_obj, &action_obj, &anim_obj, &anim_frame_obj,
                        &frame_speed_obj, &facing_obj, &ground_obj, &hitlag_obj)) {
    return NULL;
  }
  PyArrayObject* char_arr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action_arr = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* anim_arr =
      require_contiguous_array(anim_obj, NPY_UINT32, 1, "animation_index_u32");
  PyArrayObject* anim_frame_arr =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* frame_speed_arr =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* facing_arr = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* ground_arr = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag_arr = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (char_arr == NULL || action_arr == NULL || anim_arr == NULL || anim_frame_arr == NULL ||
      frame_speed_arr == NULL || facing_arr == NULL || ground_arr == NULL || hitlag_arr == NULL) {
    return NULL;
  }
  if (ecb_extents_table_init() != 0 || ecb_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "ECB table init failed");
    return NULL;
  }
  if (motion_state_owners_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "motion_state_owners_init failed");
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(char_arr);
  if (PyArray_SIZE(action_arr) != n || PyArray_SIZE(anim_arr) != n ||
      PyArray_SIZE(anim_frame_arr) != n || PyArray_SIZE(frame_speed_arr) != n ||
      PyArray_SIZE(facing_arr) != n || PyArray_SIZE(ground_arr) != n ||
      PyArray_SIZE(hitlag_arr) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "Damage hitlag CollData ECB inputs must have matching length");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* bottom_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* top_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* left_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* right_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* side_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* valid_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (bottom_arr == NULL || top_arr == NULL || left_arr == NULL || right_arr == NULL ||
      side_arr == NULL || valid_arr == NULL) {
    Py_XDECREF(bottom_arr);
    Py_XDECREF(top_arr);
    Py_XDECREF(left_arr);
    Py_XDECREF(right_arr);
    Py_XDECREF(side_arr);
    Py_XDECREF(valid_arr);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_arr);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_arr);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim_arr);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame_arr);
  const float* frame_speed_p = (const float*)PyArray_DATA(frame_speed_arr);
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing_arr);
  const uint8_t* ground_p = (const uint8_t*)PyArray_DATA(ground_arr);
  const uint16_t* hitlag_p = (const uint16_t*)PyArray_DATA(hitlag_arr);
  float* bottom_p = (float*)PyArray_DATA(bottom_arr);
  float* top_p = (float*)PyArray_DATA(top_arr);
  float* left_p = (float*)PyArray_DATA(left_arr);
  float* right_p = (float*)PyArray_DATA(right_arr);
  float* side_p = (float*)PyArray_DATA(side_arr);
  uint8_t* valid_p = (uint8_t*)PyArray_DATA(valid_arr);

  uint8_t prev_active = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t action = action_p[i];
    const uint8_t coll_handler = msl_motion_state_coll_handler_kind(char_p[i], action);
    const uint8_t active =
        (ground_p[i] == 0u && hitlag_p[i] != 0u && msl_coll_handler_is_damage(coll_handler)) ? 1u
                                                                                             : 0u;
    if (!active) {
      prev_active = 0u;
      continue;
    }
    if (!prev_active) {
      if (i > 0) {
        const uint16_t prev_action = action_p[i - 1];
        const uint8_t prev_map_callback =
            msl_motion_state_coll_source_plan(char_p[i - 1], prev_action) != 0u;
        if (prev_map_callback) {
          MslEcbWorldPoints frozen = {0};
          const float src_frame = anim_frame_p[i - 1] + frame_speed_p[i - 1];
          const uint16_t src_ecb_frame = msl_ecb_frame_u16_from_anim_frame(src_frame);
          const float facing_dir = facing_p[i - 1] ? 1.0f : -1.0f;
          msl_ecb_world_points_sample(&frozen, char_p[i - 1], anim_p[i - 1], src_ecb_frame,
                                      facing_dir, 0.0f, 0.0f, 0u);
          if (isfinite(frozen.bottom_rel_y) && isfinite(frozen.top_rel_y) &&
              isfinite(frozen.left_rel_x) && isfinite(frozen.right_rel_x) &&
              isfinite(frozen.side_rel_y) && frozen.top_rel_y > frozen.bottom_rel_y &&
              frozen.right_rel_x > frozen.left_rel_x) {
            bottom_p[i] = frozen.bottom_rel_y;
            top_p[i] = frozen.top_rel_y;
            left_p[i] = frozen.left_rel_x;
            right_p[i] = frozen.right_rel_x;
            side_p[i] = frozen.side_rel_y;
            valid_p[i] = 1u;
          }
        }
      }
    }
    prev_active = 1u;
  }

  return Py_BuildValue("(NNNNNN)", bottom_arr, top_arr, left_arr, right_arr, side_arr, valid_arr);
}

PyObject* msl_derive_downwait_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitstun_obj = Py_None;
  int down_wait_frames = 0;
  int act_down_damage_u = -1;
  int act_down_damage_d = -1;
  int act_down_wait_u = 0;
  int act_down_wait_d = 0;
  if (!PyArg_ParseTuple(args, "OOiiiii", &action_obj, &hitstun_obj, &down_wait_frames,
                        &act_down_damage_u, &act_down_damage_d, &act_down_wait_u,
                        &act_down_wait_d)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitstun = NULL;
  if (hitstun_obj != Py_None) {
    hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  }
  if (action == NULL || (hitstun_obj != Py_None && hitstun == NULL)) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (hitstun != NULL && PyArray_SIZE(hitstun) != n) {
    PyErr_SetString(PyExc_ValueError, "hitstun_u16 must match action_id_u16 length");
    return NULL;
  }
  if (down_wait_frames < 0) down_wait_frames = 0;
  if (down_wait_frames > 0x7FFF) down_wait_frames = 0x7FFF;
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT16, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hs = hitstun != NULL ? (const uint16_t*)PyArray_DATA(hitstun) : NULL;
  int16_t* out_p = (int16_t*)PyArray_DATA(out);
  int timer = 0;
  bool prev_is_dw = false;
  for (npy_intp i = 0; i < n; i++) {
    const bool is_dw = (int)a[i] == act_down_wait_u || (int)a[i] == act_down_wait_d;
    if (!is_dw) {
      timer = 0;
      prev_is_dw = false;
      continue;
    }
    if (!prev_is_dw) {
      const int prev_a = i > 0 ? (int)a[i - 1] : 0;
      const bool prev_was_down_damage =
          i > 0 && act_down_damage_u >= 0 && act_down_damage_d >= 0 &&
          (prev_a == act_down_damage_u || prev_a == act_down_damage_d);
      if (prev_was_down_damage && hs != NULL) {
        timer = (int)hs[i - 1] - 1;
        if (timer < 1) timer = 1;
        if (timer > down_wait_frames) timer = down_wait_frames;
      } else {
        timer = down_wait_frames;
      }
    } else if (timer > 0) {
      timer -= 1;
    }
    out_p[i] = (int16_t)timer;
    prev_is_dw = true;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_shine_release_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* held_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* lag_init_obj = NULL;
  int button_mask_b = 0;
  int acts[10] = {0};
  if (!PyArg_ParseTuple(args, "OOOOOiiiiiiiiiii", &action_obj, &frame_obj, &held_obj, &hitlag_obj,
                        &lag_init_obj, &button_mask_b, &acts[0], &acts[1], &acts[2], &acts[3],
                        &acts[4], &acts[5], &acts[6], &acts[7], &acts[8], &acts[9])) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* held = require_contiguous_array(held_obj, NPY_UINT16, 1, "buttons_held_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* lag_init =
      require_contiguous_array(lag_init_obj, NPY_UINT8, 1, "release_lag_init_u8");
  if (action == NULL || frame == NULL || held == NULL || hitlag == NULL || lag_init == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(held) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(lag_init) != n) {
    PyErr_SetString(PyExc_ValueError, "shine release inputs must have matching length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_lag = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_rel = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_lag == NULL || out_rel == NULL) {
    Py_XDECREF(out_lag);
    Py_XDECREF(out_rel);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(held);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* init = (const uint8_t*)PyArray_DATA(lag_init);
  uint8_t* lag_p = (uint8_t*)PyArray_DATA(out_lag);
  uint8_t* rel_p = (uint8_t*)PyArray_DATA(out_rel);
  int lag = 0;
  uint8_t is_release = 0u;
  const uint16_t mask_b = (uint16_t)((uint32_t)button_mask_b & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const int cur_a = (int)a[i];
    const int cur_af = (int)af[i];
    const int prev_a = i > 0 ? (int)a[i - 1] : cur_a;
    const int prev_af = i > 0 ? (int)af[i - 1] : cur_af;
    const bool is_ground_start = cur_a == acts[0];
    const bool is_air_start = cur_a == acts[5];
    const bool in_start = is_ground_start || is_air_start;
    const bool in_latch = cur_a == acts[0] || cur_a == acts[5] || cur_a == acts[1] ||
                          cur_a == acts[2] || cur_a == acts[4] || cur_a == acts[6] ||
                          cur_a == acts[7] || cur_a == acts[9];
    const bool in_tick = cur_a == acts[1] || cur_a == acts[2] || cur_a == acts[4] ||
                         cur_a == acts[6] || cur_a == acts[7] || cur_a == acts[9];
    bool in_any = false;
    for (int k = 0; k < 10; k++) {
      if (cur_a == acts[k]) {
        in_any = true;
        break;
      }
    }
    if (!in_any) {
      lag = 0;
      is_release = 0u;
      continue;
    }
    bool shine_start_entry = false;
    bool skip_tick = false;
    if (in_start) {
      bool prev_in_any = false;
      for (int k = 0; k < 10; k++) {
        if (prev_a == acts[k]) {
          prev_in_any = true;
          break;
        }
      }
      if (i == 0 || !prev_in_any || (cur_a == prev_a && cur_af < prev_af)) {
        shine_start_entry = true;
      }
    }
    if (shine_start_entry) {
      lag = (int)init[i];
      is_release = 0u;
      skip_tick = true;
    } else if (i == 0) {
      lag = (int)init[i] - (cur_af + 1);
      if (lag < 0) lag = 0;
      is_release = 0u;
    }
    if (in_latch && !skip_tick && hl[i] == 0u) {
      if ((buttons[i] & mask_b) == 0u) is_release = 1u;
    }
    if (in_tick && !skip_tick && hl[i] == 0u && lag > 0) {
      lag -= 1;
    }
    lag_p[i] = (uint8_t)lag;
    rel_p[i] = is_release;
  }
  return Py_BuildValue("NN", out_lag, out_rel);
}

PyObject* msl_derive_kneebend_internals_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* buttons_obj = NULL;
  PyObject* pressed_obj = NULL;
  PyObject* stick_y_obj = NULL;
  PyObject* cstick_y_obj = NULL;
  PyObject* tilt_y_obj = NULL;
  double tap_thr_d = 0.0;
  double dash_run_thr_d = 0.0;
  int tilt_max = 0;
  double release_thr_d = 0.0;
  int act_kneebend = 0;
  int act_dash = 0;
  int act_run = 0;
  int act_run_direct = 0;
  int act_run_brake = 0;
  int act_turn_run = 0;
  int button_mask_xy = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOddidiiiiiii", &action_obj, &buttons_obj, &pressed_obj,
                        &stick_y_obj, &cstick_y_obj, &tilt_y_obj, &tap_thr_d, &dash_run_thr_d,
                        &tilt_max, &release_thr_d, &act_kneebend, &act_dash, &act_run,
                        &act_run_direct, &act_run_brake, &act_turn_run, &button_mask_xy)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* pressed = require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* cstick_y = require_contiguous_array(cstick_y_obj, NPY_FLOAT32, 1, "cstick_y_unit");
  PyArrayObject* tilt_y = require_contiguous_array(tilt_y_obj, NPY_UINT8, 1, "tilt_timer_y");
  if (action == NULL || buttons == NULL || pressed == NULL || stick_y == NULL || cstick_y == NULL ||
      tilt_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(buttons) != n || PyArray_SIZE(pressed) != n || PyArray_SIZE(stick_y) != n ||
      PyArray_SIZE(cstick_y) != n || PyArray_SIZE(tilt_y) != n) {
    PyErr_SetString(PyExc_ValueError, "kneebend inputs must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_jump = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_short = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_jump == NULL || out_short == NULL) {
    Py_XDECREF(out_jump);
    Py_XDECREF(out_short);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* b = (const uint16_t*)PyArray_DATA(buttons);
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(pressed);
  const float* sy = (const float*)PyArray_DATA(stick_y);
  const float* cy = (const float*)PyArray_DATA(cstick_y);
  const uint8_t* tty = (const uint8_t*)PyArray_DATA(tilt_y);
  uint8_t* jump_p = (uint8_t*)PyArray_DATA(out_jump);
  uint8_t* short_p = (uint8_t*)PyArray_DATA(out_short);
  uint8_t jump_input = 0u;
  uint8_t is_short_hop = 0u;
  bool prev_in = false;
  const float tap_thr = (float)tap_thr_d;
  const float dash_run_thr = (float)dash_run_thr_d;
  const float rel_thr = (float)release_thr_d;
  const uint16_t xy = (uint16_t)((uint32_t)button_mask_xy & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const bool cur_in = (int)a[i] == act_kneebend;
    if (!cur_in) {
      jump_input = 0u;
      is_short_hop = 0u;
      prev_in = false;
      continue;
    }
    if (!prev_in) {
      jump_input = 0u;
      const int prev_action = i > 0 ? (int)a[i - 1] : 0xFFFF;
      const bool dash_src = prev_action == act_dash || prev_action == act_run ||
                            prev_action == act_run_direct || prev_action == act_run_brake ||
                            prev_action == act_turn_run;
      if (dash_src) {
        if ((bp[i] & xy) != 0u) {
          jump_input = 3u;
        } else if (sy[i] >= dash_run_thr && (int)tty[i] < tilt_max) {
          jump_input = 1u;
        } else if (cy[i] >= tap_thr) {
          jump_input = 2u;
        }
      } else {
        if (sy[i] >= tap_thr && (int)tty[i] < tilt_max) {
          jump_input = 1u;
        } else if ((bp[i] & xy) != 0u) {
          jump_input = 3u;
        } else if (cy[i] >= tap_thr) {
          jump_input = 2u;
        }
      }
      is_short_hop = 0u;
    }
    if (!is_short_hop) {
      if (jump_input == 3u) {
        if ((b[i] & xy) == 0u) is_short_hop = 1u;
      } else if (jump_input == 1u) {
        if (sy[i] < rel_thr) is_short_hop = 1u;
      } else if (jump_input == 2u) {
        if (cy[i] < rel_thr) is_short_hop = 1u;
      }
    }
    jump_p[i] = jump_input;
    short_p[i] = is_short_hop;
    prev_in = true;
  }
  return Py_BuildValue("NN", out_jump, out_short);
}

static inline int32_t msl_py_rate_q16_from_float(float v) {
  if (!(v > 0.0f) || !isfinite(v)) {
    return 0;
  }
  const double q = (double)v * 65536.0;
  if (q <= 0.0) {
    return 0;
  }
  if (q >= 2147483647.0) {
    return 2147483647;
  }
  return (int32_t)lrint(q);
}

PyObject* msl_derive_smash_charge_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* buttons_obj = NULL;
  int mask_a = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOi", &char_obj, &action_obj, &anim_obj, &frame_speed_obj,
                        &on_ground_obj, &hitlag_obj, &hitstun_obj, &buttons_obj, &mask_a)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_held_u16");
  if (chr == NULL || action == NULL || anim == NULL || frame_speed == NULL || on_ground == NULL ||
      hitlag == NULL || hitstun == NULL || buttons == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(anim) != n || PyArray_SIZE(frame_speed) != n ||
      PyArray_SIZE(on_ground) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n ||
      PyArray_SIZE(buttons) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "smash-charge arrays must have matching one-dimensional length");
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed for smash-charge seed lanes");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_state = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_frames = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_hold = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_saved = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT32, 0);
  if (out_state == NULL || out_frames == NULL || out_hold == NULL || out_saved == NULL) {
    Py_XDECREF(out_state);
    Py_XDECREF(out_frames);
    Py_XDECREF(out_hold);
    Py_XDECREF(out_saved);
    return NULL;
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const float* af = (const float*)PyArray_DATA(anim);
  const float* fs = (const float*)PyArray_DATA(frame_speed);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint16_t* btn = (const uint16_t*)PyArray_DATA(buttons);
  uint8_t* os = (uint8_t*)PyArray_DATA(out_state);
  uint8_t* of = (uint8_t*)PyArray_DATA(out_frames);
  uint8_t* oh = (uint8_t*)PyArray_DATA(out_hold);
  int32_t* orate = (int32_t*)PyArray_DATA(out_saved);

  uint8_t state = 0u;
  uint8_t frames = 0u;
  uint8_t hold = 0u;
  int32_t saved_rate = 1 << 16;
  int32_t last_nonzero_rate = 1 << 16;
  uint16_t prev_action = 0xFFFFu;
  float prev_anim = 0.0f;

  for (npy_intp i = 0; i < n; i++) {
    const int32_t cur_rate = msl_py_rate_q16_from_float(fs[i]);
    if (cur_rate > 0) {
      last_nonzero_rate = cur_rate;
    }

    uint8_t hold_frames = 0u;

    if (act[i] != prev_action || ground[i] == 0u || hl[i] != 0u || hs[i] != 0u) {
      state = 0u;
      frames = 0u;
      hold = 0u;
      saved_rate = last_nonzero_rate;
    }

    const uint8_t held_a = ((btn[i] & (uint16_t)mask_a) != 0u) ? 1u : 0u;
    if (state == 2u) {
      if (frames < 0xFFu) {
        frames = (uint8_t)(frames + 1u);
      }
      if (held_a == 0u || (hold != 0u && frames >= hold)) {
        if (hold != 0u && frames > hold) {
          frames = hold;
        }
        state = 3u;
      }
    } else if (state == 0u) {
      const float prev_frame = (act[i] == prev_action) ? prev_anim : af[i] - fs[i];
      if (ground[i] != 0u && hl[i] == 0u && hs[i] == 0u && held_a != 0u &&
          move_tables_grounded_smash_charge_crossed(ch[i], act[i], prev_frame, af[i],
                                                    &hold_frames)) {
        state = 2u;
        frames = 0u;
        hold = hold_frames;
        saved_rate = (cur_rate > 0) ? cur_rate : last_nonzero_rate;
      }
    }

    if (state == 2u || state == 3u) {
      os[i] = state;
      of[i] = frames;
      oh[i] = hold;
      orate[i] = saved_rate;
    }
    prev_action = act[i];
    prev_anim = af[i];
  }

  return Py_BuildValue("NNNN", out_state, out_frames, out_hold, out_saved);
}

PyObject* msl_derive_ledge_cooldown_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  int cooldown_frames = 0;
  if (!PyArg_ParseTuple(args, "OOi", &action_obj, &hitlag_obj, &cooldown_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "ledge cooldown inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* h = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  int seed = cooldown_frames;
  if (seed < 0) seed = 0;
  if (seed > 255) seed = 255;
  if (seed > 0) seed -= 1;
  for (npy_intp t = 1; t < n; t++) {
    int cd = o[t - 1];
    if (h[t - 1] == 0u && cd > 0) cd -= 1;
    if (a[t - 1] == (uint16_t)MSL_ACT_CLIFF_WAIT && a[t] >= 0x001Du && a[t] <= 0x0026u) {
      cd = seed;
    }
    if (msl_py_cliff_action_any(a[t - 1]) && msl_py_damage_action_any(a[t])) {
      // Cliff-owned damage entry:
      // ftCo_8008E908 sets x2064_ledgeCooldown while old fp->x221D_b7 is still live, before
      // Damage* Fighter_ChangeMotionState clears cliff ownership. This blocks immediate ledge
      // regrabs after a cliff option is interrupted by damage.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      cd = seed;
    }
    o[t] = (uint8_t)cd;
  }
  return (PyObject*)out;
}

static inline uint8_t msl_py_action_preserves_cliff_ledge_floor_owner(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_CLIFF_CATCH || a == (uint16_t)MSL_ACT_CLIFF_WAIT ||
          a == (uint16_t)MSL_ACT_FALL || a == (uint16_t)MSL_ACT_FALL_F ||
          a == (uint16_t)MSL_ACT_FALL_B || a == (uint16_t)MSL_ACT_JUMP_F ||
          a == (uint16_t)MSL_ACT_JUMP_B || a == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
          a == (uint16_t)MSL_ACT_JUMP_AERIAL_B || a == (uint16_t)MSL_ACT_ESCAPE_AIR)
             ? 1u
             : 0u;
}

PyObject* msl_derive_cliff_ledge_floor_segment_id_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* cooldown_obj = NULL;
  int left_floor_id = 0xFFFF;
  int right_floor_id = 0xFFFF;
  if (!PyArg_ParseTuple(args, "OOOOii", &action_obj, &facing_obj, &on_ground_obj, &cooldown_obj,
                        &left_floor_id, &right_floor_id)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* cooldown =
      require_contiguous_array(cooldown_obj, NPY_UINT8, 1, "ledge_cooldown_u8");
  if (action == NULL || facing == NULL || on_ground == NULL || cooldown == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(facing) != n || PyArray_SIZE(on_ground) != n || PyArray_SIZE(cooldown) != n) {
    PyErr_SetString(PyExc_ValueError, "cliff ledge floor owner inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT16, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* face = (const uint8_t*)PyArray_DATA(facing);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(on_ground);
  const uint8_t* cd = (const uint8_t*)PyArray_DATA(cooldown);
  uint16_t* o = (uint16_t*)PyArray_DATA(out);
  const uint16_t left = (uint16_t)((uint32_t)left_floor_id & 0xFFFFu);
  const uint16_t right = (uint16_t)((uint32_t)right_floor_id & 0xFFFFu);
  uint16_t owner = 0xFFFFu;
  for (npy_intp t = 0; t < n; t++) {
    if (msl_py_cliff_action_any(a[t])) {
      // Source cliff actions expose the ledge side through facing in Slippi post-frames:
      // left-stage ledge faces right, right-stage ledge faces left. The floor id comes only from
      // generated MSLSTG01 ledge metadata; no position/future outcome search is used.
      owner = (face[t] != 0u) ? left : right;
    } else if (ground[t] != 0u || cd[t] == 0u ||
               !msl_py_action_preserves_cliff_ledge_floor_owner(a[t])) {
      owner = 0xFFFFu;
    }
    o[t] = owner;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_cliff_option_stick_latch_x8_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* main_x_obj = NULL;
  PyObject* main_y_obj = NULL;
  PyObject* c_x_obj = NULL;
  PyObject* c_y_obj = NULL;
  double deadzone_x = 0.0;
  double deadzone_y = 0.0;
  double option_threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOddd", &action_obj, &main_x_obj, &main_y_obj, &c_x_obj, &c_y_obj,
                        &deadzone_x, &deadzone_y, &option_threshold)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* main_x = require_contiguous_array(main_x_obj, NPY_INT8, 1, "main_x_i8");
  PyArrayObject* main_y = require_contiguous_array(main_y_obj, NPY_INT8, 1, "main_y_i8");
  PyArrayObject* c_x = require_contiguous_array(c_x_obj, NPY_INT8, 1, "c_x_i8");
  PyArrayObject* c_y = require_contiguous_array(c_y_obj, NPY_INT8, 1, "c_y_i8");
  if (action == NULL || main_x == NULL || main_y == NULL || c_x == NULL || c_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(main_x) != n || PyArray_SIZE(main_y) != n || PyArray_SIZE(c_x) != n ||
      PyArray_SIZE(c_y) != n) {
    PyErr_SetString(PyExc_ValueError, "cliff option latch inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int8_t* mx = (const int8_t*)PyArray_DATA(main_x);
  const int8_t* my = (const int8_t*)PyArray_DATA(main_y);
  const int8_t* cx = (const int8_t*)PyArray_DATA(c_x);
  const int8_t* cy = (const int8_t*)PyArray_DATA(c_y);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const float dz_x = (float)deadzone_x;
  const float dz_y = (float)deadzone_y;
  const float threshold = (float)option_threshold;
  uint8_t latch = 0u;
  uint16_t prev_action = 0xFFFFu;
  for (npy_intp t = 0; t < n; t++) {
    if (a[t] != (uint16_t)MSL_ACT_CLIFF_WAIT) {
      latch = 0u;
      o[t] = 0u;
      prev_action = a[t];
      continue;
    }
    if (prev_action != (uint16_t)MSL_ACT_CLIFF_WAIT) {
      // ftCo_8009A804 initializes mv.co.cliff.x8=0 on CliffWait entry. The same visible
      // post-frame may result from a CliffCatch Anim handoff; runtime may run the neutral x8
      // setter in that same proc, but visible replay rows do not expose the pre-commit callback
      // boundary, so the prefix derivation starts the new CliffWait segment unlatched.
      latch = 0u;
    }
    float sx = stick_i8_to_unit(mx[t]);
    float sy = stick_i8_to_unit(my[t]);
    if (msl_absf(sx) < dz_x) sx = 0.0f;
    if (msl_absf(sy) < dz_y) sy = 0.0f;
    const float scx = stick_i8_to_unit(cx[t]);
    const float scy = stick_i8_to_unit(cy[t]);
    const uint8_t main_option = (uint8_t)(msl_absf(sx) >= threshold || msl_absf(sy) >= threshold);
    const uint8_t cstick_option =
        (uint8_t)(msl_absf(scx) >= threshold || msl_absf(scy) >= threshold);
    if (!main_option && !cstick_option) {
      latch = 1u;
    }
    o[t] = latch;
    prev_action = a[t];
  }
  return (PyObject*)out;
}

PyObject* msl_derive_match_flow_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  int port0 = 0;
  int dead_timer = 0;
  int dead_up_star_initial = 0;
  int dead_up_star_phase1 = 0;
  int dead_up_star_phase2 = 0;
  int dead_up_fall_entry = 0;
  int dead_up_fall_lerp = 0;
  int dead_up_fall_hitcamera = 0;
  int dead_up_fall_phase3 = 0;
  int dead_up_fall_phase4 = 0;
  int rebirth_timer = 0;
  int rebirth_wait_timer = 0;
  int entry_start_frames = 0;
  int entry_end_frames = 0;
  if (!PyArg_ParseTuple(args, "Oiiiiiiiiiiiiii", &action_obj, &port0, &dead_timer,
                        &dead_up_star_initial, &dead_up_star_phase1, &dead_up_star_phase2,
                        &dead_up_fall_entry, &dead_up_fall_lerp, &dead_up_fall_hitcamera,
                        &dead_up_fall_phase3, &dead_up_fall_phase4, &rebirth_timer,
                        &rebirth_wait_timer, &entry_start_frames, &entry_end_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const int dead_up_star_total = (dead_up_star_initial > 0 ? dead_up_star_initial : 0) +
                                 (dead_up_star_phase1 > 0 ? dead_up_star_phase1 : 0) +
                                 (dead_up_star_phase2 > 0 ? dead_up_star_phase2 : 0);
  const int dead_up_fall_total = (dead_up_fall_entry > 0 ? dead_up_fall_entry : 0) +
                                 (dead_up_fall_lerp > 0 ? dead_up_fall_lerp : 0);
  const int dead_up_fall_hitcamera_total =
      (dead_up_fall_hitcamera > 0 ? dead_up_fall_hitcamera : 0) +
      (dead_up_fall_phase3 > 0 ? dead_up_fall_phase3 : 0) +
      (dead_up_fall_phase4 > 0 ? dead_up_fall_phase4 : 0);
  const int entry_total = 5 * (port0 + 1);
  uint16_t prev = 0xFFFFu;
  int run_len = 0;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    if (i > 0 && ai == prev) {
      run_len += 1;
    } else {
      prev = ai;
      run_len = 1;
    }
    int total = -1;
    if (ai == 0u || ai == 1u || ai == 2u) {
      total = dead_timer;
    } else if (ai == 4u) {
      total = dead_up_star_total;
    } else if (ai == 6u || ai == 9u) {
      total = dead_up_fall_total;
    } else if (ai == 7u || ai == 8u || ai == 10u) {
      total = dead_up_fall_hitcamera_total;
    } else if (ai == 12u) {
      total = rebirth_timer;
    } else if (ai == 13u) {
      total = rebirth_wait_timer;
    } else if (ai == 322u) {
      total = entry_total;
    } else if (ai == 323u) {
      total = entry_start_frames > 0 ? entry_start_frames - 1 : 0;
    } else if (ai == 324u) {
      total = entry_end_frames;
    }
    if (total < 0) continue;
    int timer = total - run_len + 1;
    if (timer < 0) timer = 0;
    if (timer > 255) timer = 255;
    o[i] = (uint8_t)timer;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_match_flow_respawn_slot_cooldown_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  int shared_platform = 0;
  if (!PyArg_ParseTuple(args, "Oi", &action_obj, &shared_platform)) {
    return NULL;
  }
  PyArrayObject* action =
      require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16_by_player");
  if (action == NULL) return NULL;
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp players = PyArray_DIM(action, 1);
  if (players <= 0 || players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "action_id_u16_by_player must have 1..4 player columns");
    return NULL;
  }
  npy_intp dims[2] = {n, (npy_intp)MSL_RESPAWN_PLATFORM_SLOT_COUNT};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  if (!shared_platform) {
    return (PyObject*)out;
  }

  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  uint8_t cooldown[MSL_RESPAWN_PLATFORM_SLOT_COUNT] = {0};
  for (npy_intp frame = 0; frame < n; frame++) {
    for (uint8_t slot = 0u; slot < (uint8_t)MSL_RESPAWN_PLATFORM_SLOT_COUNT; slot++) {
      if (cooldown[slot] != 0u) {
        cooldown[slot] = (uint8_t)(cooldown[slot] - 1u);
      }
    }
    for (npy_intp p = 0; p < players; p++) {
      const uint16_t cur = a[frame * players + p];
      const uint16_t prev = (frame > 0) ? a[(frame - 1) * players + p] : 0xFFFFu;
      if (cur != (uint16_t)MSL_ACT_REBIRTH || prev == (uint16_t)MSL_ACT_REBIRTH) {
        continue;
      }
      uint8_t chosen = 0u;
      for (uint8_t slot = 0u; slot < (uint8_t)MSL_RESPAWN_PLATFORM_SLOT_COUNT; slot++) {
        if (cooldown[slot] == 0u) {
          chosen = slot;
          break;
        }
      }
      cooldown[chosen] = 0x90u;
    }
    memcpy(o + (size_t)frame * (size_t)MSL_RESPAWN_PLATFORM_SLOT_COUNT, cooldown,
           (size_t)MSL_RESPAWN_PLATFORM_SLOT_COUNT);
  }
  return (PyObject*)out;
}

PyObject* msl_derive_passivewall_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  int total = 0;
  if (!PyArg_ParseTuple(args, "OOi", &action_obj, &frame_obj, &total)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (action == NULL || frame == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n) {
    PyErr_SetString(PyExc_ValueError, "passivewall inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  if (total <= 0) return (PyObject*)out;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  int timer = 0;
  uint8_t prev_passivewall = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    const uint8_t passivewall = (uint8_t)(ai == 202u || ai == 203u);
    if (passivewall == 0u) {
      timer = 0;
      prev_passivewall = 0u;
      continue;
    }
    const uint8_t proven_reentry = (uint8_t)(i > 0 && prev_passivewall != 0u && af[i] < af[i - 1]);
    if (prev_passivewall == 0u || proven_reentry != 0u) {
      timer = total;
    } else if (timer > 0) {
      timer--;
    }
    if (timer > 255) timer = 255;
    o[i] = (uint8_t)timer;
    prev_passivewall = 1u;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_walljump_used_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* jumps_left_obj = NULL;
  PyObject* max_jumps_lut_obj = NULL;
  PyObject* buttons_pressed_obj = NULL;
  PyObject* stick_y_obj = NULL;
  int button_mask_xy = 0;
  double tap_jump_threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOid", &char_obj, &action_obj, &frame_obj, &on_ground_obj,
                        &jumps_left_obj, &max_jumps_lut_obj, &buttons_pressed_obj, &stick_y_obj,
                        &button_mask_xy, &tap_jump_threshold)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* jumps_left =
      require_contiguous_array(jumps_left_obj, NPY_UINT8, 1, "jumps_left_u8");
  PyArrayObject* max_jumps_lut =
      require_contiguous_array(max_jumps_lut_obj, NPY_UINT8, 1, "max_jumps_lut_u8");
  PyArrayObject* buttons_pressed =
      require_contiguous_array(buttons_pressed_obj, NPY_UINT16, 1, "buttons_pressed_u16");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_f32");
  if (chr == NULL || action == NULL || frame == NULL || on_ground == NULL || jumps_left == NULL ||
      max_jumps_lut == NULL || buttons_pressed == NULL || stick_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(frame) != n || PyArray_SIZE(on_ground) != n ||
      PyArray_SIZE(jumps_left) != n || PyArray_SIZE(buttons_pressed) != n ||
      PyArray_SIZE(stick_y) != n || PyArray_SIZE(max_jumps_lut) < 256) {
    PyErr_SetString(PyExc_ValueError, "walljump usage inputs must have equal lengths");
    return NULL;
  }
  if (motion_state_owners_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "motion_state_owners_init failed");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* used = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* exponent = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (used == NULL || exponent == NULL) {
    Py_XDECREF(used);
    Py_XDECREF(exponent);
    return NULL;
  }

  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(on_ground);
  const uint8_t* jumps = (const uint8_t*)PyArray_DATA(jumps_left);
  const uint8_t* max_jumps = (const uint8_t*)PyArray_DATA(max_jumps_lut);
  const uint16_t* pressed = (const uint16_t*)PyArray_DATA(buttons_pressed);
  const float* stick_y_p = (const float*)PyArray_DATA(stick_y);
  uint8_t* used_out = (uint8_t*)PyArray_DATA(used);
  uint8_t* exponent_out = (uint8_t*)PyArray_DATA(exponent);
  uint8_t count = 0u;
  uint8_t episode_exponent = 0u;
  uint8_t episode_active = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t action_i = a[i];
    const uint8_t passivewall = (action_i == (uint16_t)MSL_ACT_PASSIVE_WALL ||
                                 action_i == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP)
                                    ? 1u
                                    : 0u;
    const uint16_t prev_action = i > 0 ? a[i - 1] : UINT16_MAX;
    const uint8_t prev_passivewall = (uint8_t)(prev_action == (uint16_t)MSL_ACT_PASSIVE_WALL ||
                                               prev_action == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP);
    const uint8_t action_entry = (uint8_t)(i > 0 && (action_i != prev_action || af[i] < af[i - 1]));
    const uint8_t coll_handler = msl_motion_state_coll_handler_kind(c[i], action_i);
    const uint8_t damage_processhit_output =
        (uint8_t)(coll_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_COMMON ||
                  coll_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_FLY ||
                  coll_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_FALL);
    const uint8_t max_jumps_i = max_jumps[c[i]];
    // x1968_jumpsUsed can also move 0 -> 1 through ftCo_JumpAerial_Enter_Basic. Excluding both
    // source jump-input gates makes the remaining Damage-entry edge a grounded ftCommon bundle,
    // rather than treating any jumps-left change as landing proof.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ft_did_jump,
    //   ftCo_JumpAerial_Enter_Basic}
    const uint8_t aerial_jump_input = (uint8_t)(((pressed[i] & (uint16_t)button_mask_xy) != 0u) ||
                                                stick_y_p[i] >= (float)tap_jump_threshold);
    const uint8_t grounded_processhit_reset =
        (uint8_t)(i > 0 && ground[i] == 0u && action_entry != 0u &&
                  damage_processhit_output != 0u && max_jumps_i != 0u &&
                  jumps[i] == (uint8_t)(max_jumps_i - 1u) && jumps[i - 1] != jumps[i] &&
                  aerial_jump_input == 0u);
    if (ground[i] != 0u || action_i == (uint16_t)MSL_ACT_REBIRTH ||
        grounded_processhit_reset != 0u) {
      // ftCommon_8007D6A4 resets x1969 on grounding; Fighter_UnkInitReset_80067C98 does the same
      // before Rebirth entry. If ProcessHit launches a just-landed fighter before the post-frame
      // row, ftCommon_8007D5D4's x1968_jumpsUsed=1 write leaves the source-owned replay signature
      // jumps_left=max_jumps-1 even though the final row is airborne.
      // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D6A4}
      count = 0u;
      episode_exponent = 0u;
      episode_active = 0u;
    } else if (passivewall == 0u) {
      episode_exponent = 0u;
      episode_active = 0u;
    } else if (episode_active == 0u || (prev_passivewall != 0u && af[i] < af[i - 1])) {
      // Ordinary ftWallJump entry is the only producer that copies x1969 into the exponent and then
      // increments it. DamageFly wall techs enter through ftCo_800C1D38 with exponent zero.
      // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}
      const uint8_t ordinary_entry =
          (uint8_t)(action_i == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP && i > 0 &&
                    msl_coll_source_plan_has(
                        msl_motion_state_coll_source_plan(c[i - 1], prev_action),
                        MSL_COLL_SOURCE_WALLJUMP));
      const uint8_t walltech_entry =
          (uint8_t)(i > 0 && msl_coll_source_plan_has(
                                 msl_motion_state_coll_source_plan(c[i - 1], prev_action),
                                 MSL_COLL_SOURCE_WALLTECH));
      if (ordinary_entry != 0u) {
        episode_exponent = count;
        if (count < UINT8_MAX) {
          count++;
        }
      } else if (walltech_entry != 0u) {
        episode_exponent = 0u;
      } else {
        // A prefix that begins inside PassiveWall has no producer row. Keep the source-neutral
        // wall-tech value rather than inventing an ordinary use.
        episode_exponent = 0u;
      }
      episode_active = 1u;
    }
    used_out[i] = count;
    exponent_out[i] = passivewall ? episode_exponent : 0u;
  }

  PyObject* result = PyTuple_New(2);
  if (result == NULL) {
    Py_DECREF(used);
    Py_DECREF(exponent);
    return NULL;
  }
  PyTuple_SET_ITEM(result, 0, (PyObject*)used);
  PyTuple_SET_ITEM(result, 1, (PyObject*)exponent);
  return result;
}

PyObject* msl_derive_walljump_phase_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* setup_threshold_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* raw_x_obj = NULL;
  PyArrayObject* timer = NULL;
  PyArrayObject* side = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOO", &action_obj, &frame_obj, &setup_threshold_obj, &pos_x_obj,
                        &pos_y_obj, &raw_x_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* setup_threshold = require_contiguous_array(setup_threshold_obj, NPY_FLOAT32, 1,
                                                            "walljump_setup_x_delta_threshold_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* raw_x = require_contiguous_array(raw_x_obj, NPY_INT8, 1, "raw_main_x_i8");
  if (action == NULL || frame == NULL || setup_threshold == NULL || pos_x == NULL ||
      pos_y == NULL || raw_x == NULL) {
    goto fail;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(setup_threshold) != n || PyArray_SIZE(pos_x) != n ||
      PyArray_SIZE(pos_y) != n || PyArray_SIZE(raw_x) != n) {
    PyErr_SetString(PyExc_ValueError, "walljump phase inputs must have equal lengths");
    goto fail;
  }
  npy_intp dims[1] = {n};
  timer = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  side = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (timer == NULL || side == NULL) {
    goto fail;
  }
  uint8_t* t = (uint8_t*)PyArray_DATA(timer);
  int8_t* s = (int8_t*)PyArray_DATA(side);
  for (npy_intp i = 0; i < n; i++) t[i] = 254u;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const float* setup_x_delta = (const float*)PyArray_DATA(setup_threshold);
  const float* x = (const float*)PyArray_DATA(pos_x);
  const float* y = (const float*)PyArray_DATA(pos_y);
  const int8_t* rx = (const int8_t*)PyArray_DATA(raw_x);
  uint8_t carry_timer = 254u;
  int8_t carry_side = 0;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    if (!(ai == 27u || ai == 28u || (ai >= 29u && ai <= 34u))) {
      carry_timer = 254u;
      carry_side = 0;
      continue;
    }
    const int frame_i = af[i];
    if (frame_i < 12) {
      carry_timer = 254u;
      carry_side = 0;
      continue;
    }
    const float px = x[i];
    const float py = y[i];
    const float threshold = setup_x_delta[i];
    if (!isfinite(px) || !isfinite(py) || !isfinite(threshold) || !(threshold > 0.0f) ||
        py >= -5.0f || fabsf(px) < 60.0f) {
      carry_timer = 254u;
      carry_side = 0;
      continue;
    }

    const int8_t wall_side = (px >= 60.0f) ? (int8_t)-1 : (int8_t)1;
    const uint8_t had_carry = (carry_timer < 254u && carry_side == wall_side) ? 1u : 0u;
    const uint8_t active_timer = carry_timer;
    uint8_t setup_now = 0u;
    if (i > 0 && isfinite(x[i - 1])) {
      const float dx = px - x[i - 1];
      if ((wall_side < 0 && dx > threshold) || (wall_side > 0 && -dx > threshold)) {
        setup_now = 1u;
      }
    }

    const int cur_x = (i + 1 < n) ? (int)rx[i + 1] : (int)rx[i];
    const int prev_x = (int)rx[i];
    uint8_t old_phase_gate = 0u;
    uint8_t edge_consume_gate = 0u;
    if (wall_side < 0) {
      old_phase_gate = (uint8_t)((frame_i >= 20 && cur_x >= 64) ||
                                 (frame_i >= 17 && prev_x < 64 && cur_x >= 64));
      edge_consume_gate = (uint8_t)(prev_x < 64 && cur_x >= 64 && had_carry);
    } else {
      old_phase_gate = (uint8_t)((frame_i >= 20 && cur_x <= -64) ||
                                 (frame_i >= 19 && prev_x > -64 && cur_x <= -64));
      edge_consume_gate = (uint8_t)(prev_x > -64 && cur_x <= -64 && had_carry);
    }
    if (old_phase_gate || edge_consume_gate) {
      uint8_t out_timer = active_timer;
      if (old_phase_gate) {
        int hidden = frame_i - 8;
        if (hidden < 0) hidden = 0;
        if (hidden > 120) hidden = 120;
        out_timer = (uint8_t)hidden;
      } else if (!had_carry) {
        out_timer = 254u;
      }
      t[i] = out_timer;
      s[i] = wall_side;
    }

    if (had_carry) {
      carry_timer = (carry_timer < 253u) ? (uint8_t)(carry_timer + 1u) : 254u;
      continue;
    }

    if (setup_now) {
      carry_timer = 1u;
      carry_side = wall_side;
    } else {
      carry_timer = 254u;
      carry_side = 0;
    }
  }
  PyObject* result = PyTuple_New(2);
  if (result == NULL) {
    goto fail;
  }
  PyTuple_SET_ITEM(result, 0, (PyObject*)timer);
  PyTuple_SET_ITEM(result, 1, (PyObject*)side);
  timer = NULL;
  side = NULL;
  return result;

fail:
  Py_XDECREF(timer);
  Py_XDECREF(side);
  return NULL;
}

PyObject* msl_derive_entry_end_fall_lock_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* ground_obj = NULL;
  int act_entry_end = 0;
  int act_fall = 0;
  if (!PyArg_ParseTuple(args, "OOii", &action_obj, &ground_obj, &act_entry_end, &act_fall)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  if (action == NULL || ground == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(ground) != n) {
    PyErr_SetString(PyExc_ValueError, "entry_end_fall_lock inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const uint16_t entry = (uint16_t)((uint32_t)act_entry_end & 0xFFFFu);
  const uint16_t fall = (uint16_t)((uint32_t)act_fall & 0xFFFFu);
  uint16_t prev = 0xFFFFu;
  uint8_t lock = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    if (cur == fall && g[i] == 0u) {
      lock = (prev == entry || lock != 0u) ? 1u : 0u;
    } else {
      lock = 0u;
    }
    o[i] = lock;
    prev = cur;
  }
  return (PyObject*)out;
}

static bool vh_postframe_iasa_ran(const uint8_t* hitlag, npy_intp i) {
  if (i == 0) return hitlag[i] == 0u;
  // Priority 0 decrements a prior post-frame x195c=1 and clears x2219_b5 before priority 3
  // Fighter_Spaghetti_8006AD10 publishes input edges and invokes the IASA/input callback. A later
  // collision can then install new hitlag, so 1 -> N is an IASA frame even though both adjacent
  // post-frame snapshots are positive. Prior values >1 prove priority 3 was frozen unless the
  // current snapshot is zero after another source-owned exit path.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_Spaghetti_8006AD10,
  //   Fighter_ProcessHit_8006D1EC,Fighter_8006D10C}
  return hitlag[i - 1] <= 1u || hitlag[i] == 0u;
}

PyObject* msl_derive_jab_rapid_count_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* released_obj = NULL;
  PyObject* pressed_obj = NULL;
  int button_mask_a = 0;
  if (!PyArg_ParseTuple(args, "OOOOi", &action_obj, &hitlag_obj, &released_obj, &pressed_obj,
                        &button_mask_a)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT8, 1, "hitlag_u8");
  PyArrayObject* released =
      require_contiguous_array(released_obj, NPY_UINT16, 1, "buttons_released_u16");
  PyArrayObject* pressed =
      require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed_u16");
  if (action == NULL || hitlag == NULL || released == NULL || pressed == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n || PyArray_SIZE(released) != n || PyArray_SIZE(pressed) != n) {
    PyErr_SetString(PyExc_ValueError, "jab rapid count inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* hl = (const uint8_t*)PyArray_DATA(hitlag);
  const uint16_t* rel = (const uint16_t*)PyArray_DATA(released);
  const uint16_t* prs = (const uint16_t*)PyArray_DATA(pressed);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  uint8_t count = 0u;
  uint16_t prev = 0xFFFFu;
  const uint16_t mask = (uint16_t)((uint32_t)button_mask_a & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    const bool entered_attack11 = cur == 0x002Cu && prev != 0x002Cu;
    const bool jab = cur == 0x002Cu || cur == 0x002Du || cur == 0x002Eu;
    if (entered_attack11) {
      count = 0u;
    } else if (!jab) {
      count = 0u;
    }
    if (jab && !entered_attack11) {
      // Physical A edges advance fp->x1A54 only on frames whose IASA callback ran.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
      if (vh_postframe_iasa_ran(hl, i) && ((rel[i] | prs[i]) & mask) != 0u && count < 255u) {
        count++;
      }
      o[i] = count;
    }
    prev = cur;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_attack100_seed_latches_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* released_obj = NULL;
  PyObject* pressed_obj = NULL;
  int button_mask_a = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOi", &char_obj, &action_obj, &action_frame_obj, &hitlag_obj,
                        &released_obj, &pressed_obj, &button_mask_a)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT8, 1, "hitlag_u8");
  PyArrayObject* released =
      require_contiguous_array(released_obj, NPY_UINT16, 1, "buttons_released_u16");
  PyArrayObject* pressed =
      require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed_u16");
  if (chr == NULL || action == NULL || action_frame == NULL || hitlag == NULL || released == NULL ||
      pressed == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(action_frame) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(released) != n || PyArray_SIZE(pressed) != n) {
    PyErr_SetString(PyExc_ValueError, "Attack100 latch inputs must have equal lengths");
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed for Attack100 seed latches");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x0 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x4 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_x0 == NULL || out_x4 == NULL) {
    Py_XDECREF(out_x0);
    Py_XDECREF(out_x4);
    return NULL;
  }

  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(action_frame);
  const uint8_t* hl = (const uint8_t*)PyArray_DATA(hitlag);
  const uint16_t* rel = (const uint16_t*)PyArray_DATA(released);
  const uint16_t* prs = (const uint16_t*)PyArray_DATA(pressed);
  uint8_t* x0_out = (uint8_t*)PyArray_DATA(out_x0);
  uint8_t* x4_out = (uint8_t*)PyArray_DATA(out_x4);
  uint8_t x0 = 0u;
  uint8_t x4 = 0u;
  uint16_t prev_action = 0xFFFFu;
  int16_t prev_action_frame = -1;
  const uint16_t mask = (uint16_t)((uint32_t)button_mask_a & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    if (cur != 0x0030u) {
      x0 = 0u;
      x4 = 0u;
    } else {
      if (prev_action != 0x0030u) {
        x0 = 0u;
        x4 = 0u;
      }
      if (move_tables_attack100_loop_end_check_crossed(c[i], prev_action_frame, af[i]) != 0u) {
        // Attack100Loop_Anim consumes mv.co.attack100.x4 before the current IASA callback.
        // If replay remains in the Loop state after this checkpoint, either x4 saved the loop or
        // the callback did not take the pickup branch; both source paths clear x4 post-callback.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim
        // data/moves/<char>.json moves["ftCo_SM_Attack100Loop"].events set_throw_flags
        x4 = 0u;
      }
      // The replay-visible Attack100Loop run has an initial script pass that should not arm the
      // end-check latch; x0 becomes visible to the checkpoint owner after the loop wraps back to
      // frame 0. Preserve that prefix-causal hidden latch until the loop exits.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_Anim
      if (prev_action == 0x0030u && af[i] < prev_action_frame) {
        x0 = 1u;
      }
      // Physical A edges set x4 only on frames whose IASA callback ran.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Loop_IASA
      if (vh_postframe_iasa_ran(hl, i) && ((rel[i] | prs[i]) & mask) != 0u) {
        x4 = 1u;
      }
      x0_out[i] = x0;
      x4_out[i] = x4;
    }
    prev_action = cur;
    prev_action_frame = af[i];
  }

  PyObject* tuple = Py_BuildValue("NN", (PyObject*)out_x0, (PyObject*)out_x4);
  return tuple;
}

PyObject* msl_derive_walk_anim_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* slow_obj = NULL;
  PyObject* middle_obj = NULL;
  PyObject* fast_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOO", &action_obj, &char_obj, &facing_obj, &frame_speed_obj,
                        &slow_obj, &middle_obj, &fast_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* slow = require_contiguous_array(slow_obj, NPY_FLOAT32, 1, "walk_slow_lut");
  PyArrayObject* middle = require_contiguous_array(middle_obj, NPY_FLOAT32, 1, "walk_middle_lut");
  PyArrayObject* fast = require_contiguous_array(fast_obj, NPY_FLOAT32, 1, "walk_fast_lut");
  if (action == NULL || chr == NULL || facing == NULL || frame_speed == NULL || slow == NULL ||
      middle == NULL || fast == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(frame_speed) != n) {
    PyErr_SetString(PyExc_ValueError, "walk anim source velocity inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(slow) < 256 || PyArray_SIZE(middle) < 256 || PyArray_SIZE(fast) < 256) {
    PyErr_SetString(PyExc_ValueError, "walk divisor LUTs must have at least 256 entries");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* f = (const int8_t*)PyArray_DATA(facing);
  const float* rate_arr = (const float*)PyArray_DATA(frame_speed);
  const float* slow_lut = (const float*)PyArray_DATA(slow);
  const float* middle_lut = (const float*)PyArray_DATA(middle);
  const float* fast_lut = (const float*)PyArray_DATA(fast);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    float denom = 0.0f;
    if (a[i] == 0x000Fu) {
      denom = slow_lut[c[i]];
    } else if (a[i] == 0x0010u) {
      denom = middle_lut[c[i]];
    } else if (a[i] == 0x0011u) {
      denom = fast_lut[c[i]];
    } else {
      continue;
    }
    if (!isfinite(denom) || denom <= 0.0f) continue;
    npy_intp rate_i = i;
    if (i + 1 < n && a[i + 1] == a[i]) rate_i = i + 1;
    const float rate = rate_arr[rate_i];
    if (!isfinite(rate) || rate <= 0.0f) continue;
    const float dir = f[i] < 0 ? -1.0f : 1.0f;
    o[i] = dir * rate * denom;
  }
  return (PyObject*)out;
}

static int msl_py_walk_action_from_speed(uint8_t char_id, float gr_vel, const float* walk_max,
                                         float mid_mul, float fast_mul) {
  const float max = walk_max[char_id];
  if (!isfinite(max) || max <= 0.0f) return 0x000F;
  const float v = fabsf(gr_vel);
  if (v >= fast_mul * max) return 0x0011;
  if (v >= mid_mul * max) return 0x0010;
  return 0x000F;
}

static float msl_py_walk_rate_from_source(uint16_t action, uint8_t char_id, float facing_dir,
                                          float source_vel, const float* slow, const float* middle,
                                          const float* fast, bool* valid) {
  float denom = 0.0f;
  if (action == 0x000Fu) {
    denom = slow[char_id];
  } else if (action == 0x0010u) {
    denom = middle[char_id];
  } else if (action == 0x0011u) {
    denom = fast[char_id];
  } else {
    *valid = false;
    return 0.0f;
  }
  if (!isfinite(denom) || denom <= 0.0f) {
    *valid = false;
    return 0.0f;
  }
  *valid = true;
  if (source_vel * facing_dir <= 0.0f) return 0.0f;
  return fabsf(source_vel) / denom;
}

static int msl_py_walk_msid(uint16_t action) {
  if (action == 0x000Fu) return 7;
  if (action == 0x0010u) return 8;
  if (action == 0x0011u) return 9;
  return -1;
}

static int msl_py_predict_walk_retarget_af(uint8_t char_id, uint16_t cur_action,
                                           uint16_t dst_action, float anim_frame, float rate,
                                           const float* cycles, npy_intp cycle_w) {
  const int cur_msid = msl_py_walk_msid(cur_action);
  const int dst_msid = msl_py_walk_msid(dst_action);
  if (cur_msid < 0 || dst_msid < 0) return INT32_MIN;
  const float cur_cycle = cycles[((npy_intp)char_id * cycle_w) + cur_msid];
  const float dst_cycle = cycles[((npy_intp)char_id * cycle_w) + dst_msid];
  if (!(cur_cycle > 0.0f) || !(dst_cycle > 0.0f)) return INT32_MIN;
  const float post_tick = anim_frame + rate;
  const int quotient = (int)(post_tick / cur_cycle);
  const float adjusted = post_tick - cur_cycle * (float)quotient;
  const int final_frame = (int)(dst_cycle * (adjusted / cur_cycle));
  int out_af = final_frame + 1;
  if (out_af >= (int)dst_cycle) out_af = 0;
  return out_af;
}

PyObject* msl_derive_walk_retarget_tick_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* ref_af_obj = NULL;
  PyObject* ground_vel_obj = NULL;
  PyObject* hidden_vel_obj = NULL;
  PyObject* slow_obj = NULL;
  PyObject* middle_obj = NULL;
  PyObject* fast_obj = NULL;
  PyObject* walk_max_obj = NULL;
  PyObject* cycles_obj = NULL;
  double mid_mul = 0.0;
  double fast_mul = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOdd", &action_obj, &char_obj, &facing_obj, &anim_obj,
                        &ref_af_obj, &ground_vel_obj, &hidden_vel_obj, &slow_obj, &middle_obj,
                        &fast_obj, &walk_max_obj, &cycles_obj, &mid_mul, &fast_mul)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* ref_af =
      require_contiguous_array(ref_af_obj, NPY_INT16, 1, "ref_action_frame_i16");
  PyArrayObject* ground_vel =
      require_contiguous_array(ground_vel_obj, NPY_FLOAT32, 1, "speed_ground_x_self_f32");
  PyArrayObject* hidden_vel =
      require_contiguous_array(hidden_vel_obj, NPY_FLOAT32, 1, "walk_anim_source_vel_f32");
  PyArrayObject* slow = require_contiguous_array(slow_obj, NPY_FLOAT32, 1, "walk_slow_lut");
  PyArrayObject* middle = require_contiguous_array(middle_obj, NPY_FLOAT32, 1, "walk_middle_lut");
  PyArrayObject* fast = require_contiguous_array(fast_obj, NPY_FLOAT32, 1, "walk_fast_lut");
  PyArrayObject* walk_max = require_contiguous_array(walk_max_obj, NPY_FLOAT32, 1, "walk_max_lut");
  PyArrayObject* cycles = require_contiguous_array(cycles_obj, NPY_FLOAT32, 2, "end_frame_lut");
  if (action == NULL || chr == NULL || facing == NULL || anim == NULL || ref_af == NULL ||
      ground_vel == NULL || hidden_vel == NULL || slow == NULL || middle == NULL || fast == NULL ||
      walk_max == NULL || cycles == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  const npy_intp hidden_n = PyArray_SIZE(hidden_vel);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(anim) != n ||
      PyArray_SIZE(ref_af) != n || PyArray_SIZE(ground_vel) != n ||
      !(hidden_n == n || hidden_n + 1 == n)) {
    PyErr_SetString(PyExc_ValueError, "walk retarget inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(slow) < 256 || PyArray_SIZE(middle) < 256 || PyArray_SIZE(fast) < 256 ||
      PyArray_SIZE(walk_max) < 256 || PyArray_DIM(cycles, 0) < 256) {
    PyErr_SetString(PyExc_ValueError, "walk retarget LUTs must cover 256 character ids");
    return NULL;
  }
  const npy_intp cycle_w = PyArray_DIM(cycles, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* face = (const int8_t*)PyArray_DATA(facing);
  const float* anim_p = (const float*)PyArray_DATA(anim);
  const int16_t* ref_p = (const int16_t*)PyArray_DATA(ref_af);
  const float* gv = (const float*)PyArray_DATA(ground_vel);
  const float* hv = (const float*)PyArray_DATA(hidden_vel);
  const float* slow_lut = (const float*)PyArray_DATA(slow);
  const float* middle_lut = (const float*)PyArray_DATA(middle);
  const float* fast_lut = (const float*)PyArray_DATA(fast);
  const float* max_lut = (const float*)PyArray_DATA(walk_max);
  const float* cycle_lut = (const float*)PyArray_DATA(cycles);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i + 1 < n; i++) {
    const uint16_t action_i = a[i];
    if (!(action_i == 0x000Fu || action_i == 0x0010u || action_i == 0x0011u)) continue;
    const uint16_t target = (uint16_t)msl_py_walk_action_from_speed(
        c[i], gv[i], max_lut, (float)mid_mul, (float)fast_mul);
    if (target == action_i || !(target == 0x000Fu || target == 0x0010u || target == 0x0011u)) {
      continue;
    }
    const float dir = face[i] < 0 ? -1.0f : 1.0f;
    bool hidden_valid = false;
    bool ground_valid = false;
    const float hidden_rate = msl_py_walk_rate_from_source(action_i, c[i], dir, hv[i], slow_lut,
                                                           middle_lut, fast_lut, &hidden_valid);
    const float ground_rate = msl_py_walk_rate_from_source(action_i, c[i], dir, gv[i], slow_lut,
                                                           middle_lut, fast_lut, &ground_valid);
    const int hidden_af = hidden_valid
                              ? msl_py_predict_walk_retarget_af(c[i], action_i, target, anim_p[i],
                                                                hidden_rate, cycle_lut, cycle_w)
                              : INT32_MIN;
    const int ground_af = ground_valid
                              ? msl_py_predict_walk_retarget_af(c[i], action_i, target, anim_p[i],
                                                                ground_rate, cycle_lut, cycle_w)
                              : INT32_MIN;
    const int ref = (int)ref_p[i + 1];
    if (ground_af == ref && hidden_af != ref) {
      o[i] = gv[i];
    } else if (hidden_af == ref && ground_af != ref) {
      o[i] = hv[i];
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_run_anim_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* scaling_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOO", &action_obj, &char_obj, &facing_obj, &frame_speed_obj,
                        &scaling_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* scaling = require_contiguous_array(scaling_obj, NPY_FLOAT32, 1, "run_scaling_lut");
  if (action == NULL || chr == NULL || facing == NULL || frame_speed == NULL || scaling == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(frame_speed) != n) {
    PyErr_SetString(PyExc_ValueError, "run anim source velocity inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(scaling) < 256) {
    PyErr_SetString(PyExc_ValueError, "run scaling LUT must have at least 256 entries");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* f = (const int8_t*)PyArray_DATA(facing);
  const float* rate_arr = (const float*)PyArray_DATA(frame_speed);
  const float* scale_lut = (const float*)PyArray_DATA(scaling);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if (!(a[i] == 0x0015u || a[i] == 0x0016u)) continue;
    const float scaling_v = scale_lut[c[i]];
    if (!isfinite(scaling_v) || scaling_v <= 0.0f) continue;
    npy_intp rate_i = i;
    if (i + 1 < n && a[i + 1] == a[i]) rate_i = i + 1;
    const float rate = rate_arr[rate_i];
    if (!isfinite(rate) || rate <= 0.0f) continue;
    const float dir = f[i] < 0 ? -1.0f : 1.0f;
    o[i] = dir * rate * scaling_v;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_facing_dir1_sign_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* facing_obj = NULL;
  PyObject* action_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &facing_obj, &action_obj)) {
    return NULL;
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (facing == NULL || action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(facing);
  if (PyArray_SIZE(action) != n) {
    PyErr_SetString(PyExc_ValueError, "facing_dir1 inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (out == NULL) return NULL;
  const uint8_t* face = (const uint8_t*)PyArray_DATA(facing);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  int8_t* o = (int8_t*)PyArray_DATA(out);
  uint16_t prev = 0xFFFFu;
  int8_t cur_sign = 1;
  for (npy_intp i = 0; i < n; i++) {
    const int8_t face_sign = face[i] != 0u ? 1 : -1;
    if (i == 0 || a[i] != prev) cur_sign = face_sign;
    o[i] = cur_sign;
    prev = a[i];
  }
  return (PyObject*)out;
}

PyObject* msl_derive_common_fall_blend_seed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* speed_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* air_drift_max_obj = NULL;
  double threshold_arg = 0.0;
  double lerp_arg = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOdd", &char_obj, &action_obj, &speed_obj, &facing_obj,
                        &air_drift_max_obj, &threshold_arg, &lerp_arg)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* speed =
      require_contiguous_array(speed_obj, NPY_FLOAT32, 1, "speed_air_x_self_f32");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_FLOAT32, 1, "facing_dir_f32");
  PyArrayObject* air_max_lut =
      require_contiguous_array(air_drift_max_obj, NPY_FLOAT32, 1, "air_drift_max_by_char");
  if (chr == NULL || action == NULL || speed == NULL || facing == NULL || air_max_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(speed) != n || PyArray_SIZE(facing) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "common-fall blend seed inputs must have matching one-dimensional length");
    return NULL;
  }
  if (PyArray_SIZE(air_max_lut) < 256) {
    PyErr_SetString(PyExc_ValueError, "air_drift_max_by_char must have at least 256 entries");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_valid = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x4 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_msid = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT16, 0);
  if (out_valid == NULL || out_x4 == NULL || out_msid == NULL) {
    Py_XDECREF(out_valid);
    Py_XDECREF(out_x4);
    Py_XDECREF(out_msid);
    return NULL;
  }

  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const float* sx = (const float*)PyArray_DATA(speed);
  const float* fd = (const float*)PyArray_DATA(facing);
  const float* air_max = (const float*)PyArray_DATA(air_max_lut);
  uint8_t* ov = (uint8_t*)PyArray_DATA(out_valid);
  float* ox = (float*)PyArray_DATA(out_x4);
  uint16_t* om = (uint16_t*)PyArray_DATA(out_msid);

  float x4 = 0.0f;
  uint16_t stored_msid = 0u;
  uint8_t prev_common = 0u;
  uint16_t prev_action = 0xFFFFu;
  uint8_t prev_char = 0xFFu;
  const float threshold = (float)threshold_arg;
  const float lerp = (float)lerp_arg;

  for (npy_intp i = 0; i < n; i++) {
    uint16_t neutral = 0u;
    uint16_t forwards = 0u;
    uint16_t backwards = 0u;
    if (!msl_action_common_fall_blend_msids(act[i], &neutral, &forwards, &backwards)) {
      x4 = 0.0f;
      stored_msid = 0u;
      prev_common = 0u;
      prev_action = act[i];
      prev_char = ch[i];
      continue;
    }
    if (prev_common == 0u || act[i] != prev_action || ch[i] != prev_char) {
      x4 = 0.0f;
      stored_msid = neutral;
    }

    uint16_t target_msid = neutral;
    float target = 0.0f;
    const float max = air_max[ch[i]];
    if (isfinite(max) && max > 0.0f) {
      float frac = sx[i] / max;
      if (frac > 1.0f) {
        frac = 1.0f;
      } else if (frac < -1.0f) {
        frac = -1.0f;
      }
      const float abs_frac = fabsf(frac);
      if (abs_frac > threshold && threshold < 1.0f) {
        target = (abs_frac - threshold) / (1.0f - threshold);
        const float facing_dir = fd[i] < 0.0f ? -1.0f : 1.0f;
        target_msid = (frac * facing_dir > 0.0f) ? forwards : backwards;
      }
    }

    x4 += lerp * (target - x4);
    if (x4 < 0.0f) {
      x4 = 0.0f;
    } else if (x4 > 1.0f) {
      x4 = 1.0f;
    }
    if (x4 != 0.0f && target_msid != stored_msid) {
      stored_msid = target_msid;
    }
    ov[i] = 1u;
    ox[i] = x4;
    om[i] = stored_msid;
    prev_common = 1u;
    prev_action = act[i];
    prev_char = ch[i];
  }

  return Py_BuildValue("NNN", out_valid, out_x4, out_msid);
}
