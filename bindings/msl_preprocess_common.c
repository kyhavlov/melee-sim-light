/* Native preprocessing derivation wrappers for the msl_binding extension. */

#include "msl_preprocess.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/action_ids.h"
#include "../src/api.h"
#include "../src/anim_pose.h"
#include "../src/anim_table.h"
#include "../src/attack_id_tables.h"
#include "../src/char_params.h"
#include "../src/combat_geom.h"
#include "../src/common_params.h"
#include "../src/hit_elements.h"
#include "../src/hitboxes_tables.h"
#include "../src/hitlist.h"
#include "../src/hurtcaps_tables.h"
#include "../src/input_axis.h"
#include "../src/mpcoll_ecb_points.h"
#include "../src/move_tables.h"
#include "../src/motion_state_owners.h"
#include "../src/shield_tilt_table.h"
#include "../src/specialhi_pose.h"
#include "../src/stage_item_params.h"
#include "../src/msl_math.h"
#include "../src/staling.h"
#include "../src/ucf.h"

int parse_u16_sequence_fixed(PyObject* obj, uint16_t* out, Py_ssize_t cap, Py_ssize_t* out_count,
                             const char* name) {
  PyObject* seq = PySequence_Fast(obj, name);
  if (seq == NULL) {
    return -1;
  }
  const Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
  if (n > cap) {
    PyErr_Format(PyExc_ValueError, "%s has too many entries (got %zd, max %zd)", name, n, cap);
    Py_DECREF(seq);
    return -1;
  }
  for (Py_ssize_t i = 0; i < n; i++) {
    const long v = PyLong_AsLong(PySequence_Fast_GET_ITEM(seq, i));
    if (PyErr_Occurred()) {
      Py_DECREF(seq);
      return -1;
    }
    out[i] = (uint16_t)((uint32_t)v & 0xFFFFu);
  }
  Py_DECREF(seq);
  *out_count = n;
  return 0;
}
bool u16_in_fixed_set(uint16_t v, const uint16_t* set, Py_ssize_t count) {
  for (Py_ssize_t i = 0; i < count; i++) {
    if (set[i] == v) {
      return true;
    }
  }
  return false;
}

bool item_key_less(uint16_t iid_a, uint32_t spawn_a, uint16_t type_a, uint16_t iid_b,
                   uint32_t spawn_b, uint16_t type_b) {
  return iid_a < iid_b ||
         (iid_a == iid_b && (spawn_a < spawn_b || (spawn_a == spawn_b && type_a < type_b)));
}

void msl_py_mtx34_mul_point(const float m[12], float x, float y, float z, float* out_x,
                            float* out_y, float* out_z) {
  *out_x = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
  *out_y = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
  *out_z = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
}

uint8_t msl_py_apply_specialhi_xrotn(uint8_t char_id, uint16_t action_id, uint16_t msid,
                                     uint16_t frame, uint16_t part_id, float model_scale,
                                     float rotate_model, uint8_t rotate_model_valid, float* io_x,
                                     float* io_y, float* io_z) {
  if (rotate_model_valid == 0u || !msl_specialhi_rotate_model_action(char_id, action_id) ||
      !msl_anim_part_under_xrotn(char_id, part_id) || !isfinite(rotate_model)) {
    return 0u;
  }
  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, 2u, m) != 0) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  msl_py_mtx34_mul_point(m, 0.0f, 0.0f, 0.0f, &ax0, &ay0, &az0);
  msl_py_mtx34_mul_point(m, 1.0f, 0.0f, 0.0f, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = msl_melee_cosf(angle);
  const float s = msl_melee_sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

void msl_py_apply_live_transn_tail(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t part_id,
                                   float model_scale, float* io_x, float* io_y, float* io_z) {
  if (io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id) ||
      msl_anim_uses_root_motion(char_id, msid) != 0u) {
    return;
  }

  float transn[3];
  if (anim_pose_get_transn(char_id, msid, frame, transn) != 0) {
    return;
  }

  // Source owner: ftColl_8007AD18 keeps HitCapsule.x58/x4C in the same live-JObj space consumed
  // by ftColl_80076ED8/lbColl_8000805C. MSL SSANIM01 matrices strip FtPart_TransN into the
  // extracted TransN tail, so teacher-forced seed reconstruction must recompose that tail for
  // non-root-motion hitboxes under FtPart_XRotN just like free-running hitboxes_refresh().
  // Probe-backed witness: RipeWealthySeahorse.msl:2119 Marth AttackS4 hb3 x58.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  // data/anims/<char>.tracks.bin uses_root_motion / data/anims/<char>.bin TransN tail
  *io_x += transn[0] * model_scale;
  *io_y += transn[1] * model_scale;
  *io_z += transn[2] * model_scale;
}

float msl_py_segment_x_at_y(float y, float x0, float y0, float x1, float y1,
                            bool require_vertical_lower_endpoint) {
  if (require_vertical_lower_endpoint && fabsf(x1 - x0) < 1.0e-6f) {
    const float min_y = y0 < y1 ? y0 : y1;
    if (y > min_y + 1.0f) return NAN;
  }
  const float lo = (y0 < y1 ? y0 : y1) - 0.25f;
  const float hi = (y0 > y1 ? y0 : y1) + 0.25f;
  if (y < lo || y > hi) return NAN;
  if (fabsf(y1 - y0) < 1.0e-6f) return x0;
  float t = (y - y0) / (y1 - y0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return x0 + (x1 - x0) * t;
}

bool msl_py_action_is_airborne_damage_family(uint16_t action_id) {
  return action_id >= (uint16_t)MSL_ACT_DAMAGE_AIR_1 &&
         action_id <= (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL;
}
