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
// hitbox centers and to drive fighter-vs-fighter combat resolution (via hitbox-vs-hurtcap overlap
// checks in `src/combat.c`).
int hitboxes_tables_init(void);

// Binary artifact schema version expected by this runtime.
uint32_t hitboxes_tables_format_version(void);

// Test/debug helper: reset global hitbox tables so a subsequent hitboxes_tables_init() reloads from
// the current MSL_DATA_DIR. This exists only for fast synthetic tests; it must not be used while
// any live batches are depending on hitbox tables.
void hitboxes_tables_reset_for_tests(void);

typedef struct MslHitboxEvent {
  // Frame timeline key. Interpreted against decomp-shaped anim/script time:
  // fp->cur_anim_frame (Slippi post-frame `state_age`, float).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
  uint16_t frame;
  uint8_t kind;       // MslHitboxEventKind
  uint8_t hitbox_id;  // 0..3 typical; 0xFF used by clear-all records.

  // Attachment (Fighter_Part id domain; used as anim_pose_get_matrix(..., part_id)).
  uint16_t bone_part_id;

  // Bone-local offsets and hitbox params. World-space center is computed via anim_pose_get_matrix(...)
  // and then translated by fighter (pos_x, pos_y, pos_z).
  //
  // IMPORTANT (offset basis): `data/hitboxes/<char>.bin` stores these offsets in HitCapsule.b_offset
  // component order (b_offset.x/b_offset.y/b_offset.z), not the raw script field names
  // (x_offset/y_offset/z_offset). The extractor performs the decomp-shaped mapping:
  //   b_offset.x := z_offset; b_offset.y := y_offset; b_offset.z := x_offset.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  float x;
  float y;
  float z;
  float radius;
  float damage;  // Create damage, or active-slot replacement damage for SET_DAMAGE records.

  // Remaining extracted u16 parameters (packed directly from the .bin record).
  // The exact semantics are decomp-first but not yet wired into gameplay logic.
  // Layout (see agent_docs/DATA_CONTRACT.md `MSLHITB1 v2`):
  // - u16_0: angle
  // - u16_1: kbg
  // - u16_2: wsk
  // - u16_3: bkb
  // - u16_4: element (low 8) | shield_damage_u8 (high 8, 2's complement)
  // - u16_5: sfx_severity (low 8) | sfx_kind (high 8)
  // - u16_6: flags bitfield (hit_grounded/hit_aerial/item_hit/clank/rebound/etc.)
  // - u16_7: hitlist metadata pack:
  //   - low 8 bits: `rehit_rate_frames` (decomp: HitCapsule.x40_b4)
  //   - bits 8..10: `hit_group` (decomp: spawn_hitbox_0.hit_group / HitCapsule.x4)
  //   - remaining bits: reserved (0)
  uint16_t u16_0;
  uint16_t u16_1;
  uint16_t u16_2;
  uint16_t u16_3;
  uint16_t u16_4;
  uint16_t u16_5;
  uint16_t u16_6;
  uint16_t u16_7;
} MslHitboxEvent;

typedef enum MslHitboxEventKind {
  MSL_HITBOX_EVENT_CREATE = 0,
  MSL_HITBOX_EVENT_CLEAR = 1,
  MSL_HITBOX_EVENT_SET_DAMAGE = 2,
  MSL_HITBOX_EVENT_SET_INTERACTION = 3,
} MslHitboxEventKind;

// MSLHITB1 v2 `u16_6` flag bits (see agent_docs/DATA_CONTRACT.md).
//
// Source pointers:
// - tools/extraction/extract_fighter_moves.py::_decode_create_hitbox (script opcode 11 decode)
// - refs/melee/src/melee/ft/types.h (gmScriptEventDefault packing for spawn_hitbox_0 words)
enum {
  // Runtime HitCapsule interaction state. CREATE initializes both x42 bits to 1; interaction
  // mutation records update them independently. VALID lets current synthetic/debug records omit
  // the low bits while generated MSLHITB1 records carry the source-owned values.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_8007121C,ftAction_80071708}
  MSL_HITBOX_FLAG_X42_B5 = 1u << 0,
  MSL_HITBOX_FLAG_X42_B7 = 1u << 1,
  MSL_HITBOX_FLAG_X42_INTERACTION_VALID = 1u << 2,
  MSL_HITBOX_FLAG_HIT_GROUNDED = 1u << 9,
  MSL_HITBOX_FLAG_HIT_AERIAL = 1u << 10,
  MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION = 1u << 11,
  MSL_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS = 1u << 12,
  MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE = 1u << 13,
  MSL_HITBOX_FLAG_CLANK = 1u << 14,
  MSL_HITBOX_FLAG_REBOUND = 1u << 15,
};

// x42_b5 is consumed by fighter-owned collision passes, including ftColl_8007925C's item
// HitCapsule vs fighter HitCapsule branch. x42_b7 is consumed by the separate itColl fighter
// HitCapsule vs item-hurtbox path. Name the helpers after the source fields because neither bit is
// equivalent to a generic "fighter" or "item" interaction category.
// refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
// refs/melee/src/melee/it/itcoll.c::it_8026D564
static inline uint8_t msl_hitbox_x42_b5_enabled(uint16_t flags) {
  return ((flags & (uint16_t)MSL_HITBOX_FLAG_X42_INTERACTION_VALID) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_X42_B5) != 0u)
             ? 1u
             : 0u;
}

static inline uint8_t msl_hitbox_x42_b7_enabled(uint16_t flags) {
  return ((flags & (uint16_t)MSL_HITBOX_FLAG_X42_INTERACTION_VALID) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_X42_B7) != 0u)
             ? 1u
             : 0u;
}

static inline uint8_t msl_hitbox_ignore_fighter_scale(uint16_t flags) {
  return (flags & (uint16_t)MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE) != 0 ? 1u : 0u;
}

// Gets the move's hitbox event list for (char_id, msid).
// Returns 0 on success with (out_events, out_count) set, or nonzero on failure/missing.
int hitboxes_get_events(uint8_t char_id, uint16_t msid, const MslHitboxEvent** out_events,
                        uint16_t* out_count);

#ifdef __cplusplus
}  // extern "C"
#endif
