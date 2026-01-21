#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init-time loader for extracted hitbox event tables:
// - data/hitboxes/fox.bin (char_id=1)
// - data/hitboxes/falco.bin (char_id=22)
//
// This is load-only for now: the simulator uses these tables to refresh per-frame world-space
// hitbox centers for debug readback, but does not perform combat resolution yet.
int hitboxes_tables_init(void);

typedef struct MslHitboxEvent {
  // Frame timeline key. Interpreted against seeded `action_frame` (integer frame).
  uint16_t frame;
  uint8_t kind;      // 0 = set/enable, 1 = clear
  uint8_t hitbox_id; // 0..3 typical; 0xFF used by clear-all records.

  // Attachment (Fighter_Part id domain; used as anim_pose_get_matrix(..., part_id)).
  uint16_t bone_part_id;

  // Bone-local offsets and hitbox params. World-space center is computed via anim_pose_get_matrix(...)
  // and then translated by fighter (pos_x, pos_y).
  float x;
  float y;
  float z;
  float radius;
  float damage;

  // Remaining extracted u16 parameters (packed directly from the .bin record).
  // The exact semantics are decomp-first but not yet wired into gameplay logic.
  uint16_t u16_0;
  uint16_t u16_1;
  uint16_t u16_2;
  uint16_t u16_3;
  uint16_t u16_4;
  uint16_t u16_5;
  uint16_t u16_6;
  uint16_t u16_7;
} MslHitboxEvent;

// Gets the move's hitbox event list for (char_id, msid).
// Returns 0 on success with (out_events, out_count) set, or nonzero on failure/missing.
int hitboxes_get_events(uint8_t char_id, uint16_t msid, const MslHitboxEvent** out_events,
                        uint16_t* out_count);

#ifdef __cplusplus
}  // extern "C"
#endif

