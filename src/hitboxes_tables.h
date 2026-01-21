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
  // Layout (see docs/DATA_CONTRACT.md `MSLHITB1 v1`):
  // - u16_0: angle
  // - u16_1: kbg
  // - u16_2: wsk
  // - u16_3: bkb
  // - u16_4: element (low 8) | shield_damage_u8 (high 8, 2's complement)
  // - u16_5: sfx_severity (low 8) | sfx_kind (high 8)
  // - u16_6: flags bitfield (hit_grounded/hit_aerial/item_hit/clank/rebound/etc.)
  // - u16_7: reserved (0)
  uint16_t u16_0;
  uint16_t u16_1;
  uint16_t u16_2;
  uint16_t u16_3;
  uint16_t u16_4;
  uint16_t u16_5;
  uint16_t u16_6;
  uint16_t u16_7;
} MslHitboxEvent;

// MSLHITB1 v1 `u16_6` flag bits (see docs/DATA_CONTRACT.md).
//
// Source pointers:
// - tools/extraction/extract_fighter_moves.py::_decode_create_hitbox (script opcode 11 decode)
// - refs/melee/src/melee/ft/types.h (gmScriptEventDefault packing for spawn_hitbox_0 words)
enum {
  MSL_HITBOX_FLAG_HIT_GROUNDED = 1u << 9,
  MSL_HITBOX_FLAG_HIT_AERIAL = 1u << 10,
  MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION = 1u << 11,
  MSL_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS = 1u << 12,
  MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE = 1u << 13,
  MSL_HITBOX_FLAG_CLANK = 1u << 14,
  MSL_HITBOX_FLAG_REBOUND = 1u << 15,
};

// Gets the move's hitbox event list for (char_id, msid).
// Returns 0 on success with (out_events, out_count) set, or nonzero on failure/missing.
int hitboxes_get_events(uint8_t char_id, uint16_t msid, const MslHitboxEvent** out_events,
                        uint16_t* out_count);

#ifdef __cplusplus
}  // extern "C"
#endif
