#pragma once

#include <stdint.h>

typedef struct MslMpcollBoundingAabb {
  float left;
  float bottom;
  float right;
  float top;
} MslMpcollBoundingAabb;

static inline void mpcoll_check_bounding_aabb(
    float prev_pos_x, float prev_pos_y, float cur_pos_x, float cur_pos_y, float prev_ecb_left_rel_x,
    float prev_ecb_right_rel_x, float prev_ecb_bottom_rel_y, float prev_ecb_top_rel_y,
    float cur_ecb_left_rel_x, float cur_ecb_right_rel_x, float cur_ecb_bottom_rel_y,
    float cur_ecb_top_rel_y, uint32_t flags, float ledge_snap_x, float ledge_snap_y,
    float ledge_snap_height, MslMpcollBoundingAabb* out) {
  if (out == NULL) {
    return;
  }

  // Source `mpCollCheckBounding`: build the swept CollData ECB AABB from current and previous root
  // positions, then optionally expand by the ledge snap envelope for `flags & 0b100`.
  // refs/melee/src/melee/mp/mpcoll.c::mpCollCheckBounding
  float left = cur_pos_x + cur_ecb_left_rel_x;
  const float prev_left = prev_pos_x + prev_ecb_left_rel_x;
  if (left > prev_left) {
    left = prev_left;
  }

  float right = cur_pos_x + cur_ecb_right_rel_x;
  const float prev_right = prev_pos_x + prev_ecb_right_rel_x;
  if (right < prev_right) {
    right = prev_right;
  }

  float bottom = cur_pos_y + cur_ecb_bottom_rel_y;
  const float prev_bottom = prev_pos_y + prev_ecb_bottom_rel_y;
  if (bottom > prev_bottom) {
    bottom = prev_bottom;
  }

  float top = cur_pos_y + cur_ecb_top_rel_y;
  const float prev_top = prev_pos_y + prev_ecb_top_rel_y;
  if (top < prev_top) {
    top = prev_top;
  }

  if ((flags & 0x4u) != 0u) {
    const float half_height = 0.5f * ledge_snap_height;
    right += ledge_snap_x;
    left -= ledge_snap_x;

    const float snap_bottom = ledge_snap_y - half_height;
    const float cur_snap_bottom = cur_pos_y + snap_bottom;
    if (bottom > cur_snap_bottom) {
      bottom = cur_snap_bottom;
    }
    const float prev_snap_bottom = prev_pos_y + snap_bottom;
    if (bottom > prev_snap_bottom) {
      bottom = prev_snap_bottom;
    }

    const float snap_top = ledge_snap_y + half_height;
    const float cur_snap_top = cur_pos_y + snap_top;
    if (top < cur_snap_top) {
      top = cur_snap_top;
    }
    const float prev_snap_top = prev_pos_y + snap_top;
    if (top < prev_snap_top) {
      top = prev_snap_top;
    }
  }

  *out = (MslMpcollBoundingAabb){
      .left = left,
      .bottom = bottom,
      .right = right,
      .top = top,
  };
}
