#pragma once

// ECB point extraction helpers for mpColl-shaped FD collision substrates.
//
// Scope note:
// - This is currently **FD-only** (used by the FD stage collision substrates) and intended to
//   support the replay-suite characters (Fox/Falco) with ISO-extracted ECB tables.
// - The helper is written in a stage-agnostic way, but callers should not assume full stage parity
//   or complete mpColl coverage yet; it is a shared point source to avoid per-module ECB drift.
//
// Data sources:
// - ECB extents/bottom: `data/ecb/*` (see ecb_tables.h)
// - ECB side-point Y offset (ftData_x44_t.unkC): `data/characters/*` via char_params.h
//
// Decomp refs:
// - refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
// - refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_Fixed
// - refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
// - refs/melee/src/melee/ft/types.h::ftData_x44_t

#include <stdint.h>

#include "anim_frame.h"
#include "char_params.h"
#include "ecb_tables.h"

typedef struct MslEcbWorldPoints {
  // World-space ECB points.
  float bottom_x;
  float bottom_y;
  float top_x;
  float top_y;
  float left_x;
  float left_y;
  float right_x;
  float right_y;

  // Fighter-local sampled offsets (after facing mirroring for X).
  float left_rel_x;
  float right_rel_x;
  float bottom_rel_y;
  float top_rel_y;
  float side_rel_y;

  uint16_t frame_u16;
} MslEcbWorldPoints;

typedef struct MslEcbBottomWorldPoint {
  float x;
  float y;
  float rel_y;
  uint16_t frame_u16;
} MslEcbBottomWorldPoint;

static inline uint16_t msl_ecb_frame_u16_from_anim_frame(float anim_frame_f32) {
  const float af = msl_anim_frame_sanitize_f32(anim_frame_f32);
  return msl_anim_frame_floor_u16(af);
}

static inline uint16_t msl_ecb_prev_frame_u16(uint16_t frame_u16) {
  return (frame_u16 > 0u) ? (uint16_t)(frame_u16 - 1u) : 0u;
}

static inline void msl_ecb_bottom_world_point_sample(MslEcbBottomWorldPoint* out, uint8_t char_id,
                                                     uint32_t animation_index, uint16_t frame_u16,
                                                     float pos_x, float pos_y,
                                                     uint8_t lock_bottom_to_zero) {
  if (out == NULL) {
    return;
  }
  out->frame_u16 = frame_u16;
  // Decomp: desired_ecb.bottom.x is always 0.0 (JObj/FIXED), so world bottom X is cur_pos.x.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_Fixed
  const int frame_i = (int)frame_u16;
  const float rel_y =
      lock_bottom_to_zero ? 0.0f : msl_ecb_bottom_rel_y(char_id, animation_index, frame_i);
  out->x = pos_x;
  out->y = pos_y + rel_y;
  out->rel_y = rel_y;
}

static inline void msl_ecb_world_points_sample(MslEcbWorldPoints* out, uint8_t char_id,
                                               uint32_t animation_index, uint16_t frame_u16,
                                               float facing_dir, float pos_x, float pos_y,
                                               uint8_t lock_bottom_to_zero) {
  if (out == NULL) {
    return;
  }
  out->frame_u16 = frame_u16;

  // Decomp ECB point mapping:
  // - desired_ecb.bottom.x/top.x are always 0.0 (JObj and Fixed paths).
  // - desired_ecb.bottom.y comes from the "bottom" extent (or is forced to 0 under flags&1).
  // - desired_ecb.top.y comes from the "top" extent.
  // - desired_ecb.left.x / desired_ecb.right.x come from left/right extents and are mirrored by facing.
  // - desired_ecb.left.y / desired_ecb.right.y are the midpoint between bottom/top plus an offset.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_Fixed
  const int frame_i = (int)frame_u16;
  const MslEcbExtentsRel ext = msl_ecb_extents_rel(char_id, animation_index, frame_i);

  float bottom_rel_y =
      lock_bottom_to_zero ? 0.0f : msl_ecb_bottom_rel_y(char_id, animation_index, frame_i);
  const float top_rel_y = ext.max_y;

  float left_rel_x = 0.0f;
  float right_rel_x = 0.0f;
  {
    float lx = facing_dir * ext.min_x;
    float rx = facing_dir * ext.max_x;
    if (lx <= rx) {
      left_rel_x = lx;
      right_rel_x = rx;
    } else {
      left_rel_x = rx;
      right_rel_x = lx;
    }
  }

  float side_offset_y = 0.0f;
  {
    const MslCharParams* ch = msl_char_params(char_id);
    if (ch != NULL) {
      side_offset_y = ch->ecb_side_y_offset;
    }
  }
  const float side_rel_y = side_offset_y + 0.5f * (top_rel_y + bottom_rel_y);

  out->left_rel_x = left_rel_x;
  out->right_rel_x = right_rel_x;
  out->bottom_rel_y = bottom_rel_y;
  out->top_rel_y = top_rel_y;
  out->side_rel_y = side_rel_y;

  out->bottom_x = pos_x;
  out->bottom_y = pos_y + bottom_rel_y;
  out->top_x = pos_x;
  out->top_y = pos_y + top_rel_y;
  out->left_x = pos_x + left_rel_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + right_rel_x;
  out->right_y = pos_y + side_rel_y;
}
