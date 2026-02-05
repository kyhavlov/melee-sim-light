#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decomp-shaped HitCapsule victim list representation.
//
// Decomp anchors (GALE01):
// - HitCapsule layout: refs/melee/src/melee/lb/types.h::HitCapsule
//   - victims_1[12] at +0x74
//   - victims_2[12] at +0xD4
//   - ring insertion indices: x44 / x45
// - Clear: refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
// - Insert/refresh (victims_1): refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
// - Insert/refresh (victims_2): refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820
// - Decrement + expiry clear: refs/melee/src/melee/lb/lbcollision.c::lbColl_80008A5C
//
// Notes:
// - Decomp stores a raw victim pointer (`HitVictim.victim`) and uses pointer equality.
// - This simulator stores a compact victim identity key and uses instance_id as a proxy for
//   death/respawn pointer-change boundaries (see src/hitlist.c for the approximation policy).
enum {
  // Decomp-sized: HitCapsule.victims_{1,2}[12].
  MSL_HITLIST_VICTIM_CAP = 12,
};

// lbColl victim-list insertion "type" codes.
//
// Decomp stores the type as an int and uses it to decide whether to refresh an existing entry's
// cooldown (`HitVictim.x4 = HitCapsule.x40_b4`) when the victim is already present.
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_80008820}
//
// These values are intentionally named even when their higher-level meaning is unclear in this
// slice; do not reintroduce raw numeric literals in gameplay logic.
typedef enum MslLbCollInsertType {
  // Fighter BODY hit insert (ftColl_80076ED8 -> inlineB0 -> lbColl_80008688(..., type=0, ...)).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  MSL_LBCOLL_INSERT_FT_BODY = 0,
  // Fighter CATCH hit insert (ftColl_80078A2C -> ftColl_80076808(..., type=0, ...)).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  //
  // Note: this is intentionally an alias of MSL_LBCOLL_INSERT_FT_BODY (same numeric type) because
  // decomp passes type=0 in both paths.
  MSL_LBCOLL_INSERT_FT_CATCH = 0,
  // Fighter SHIELD hit insert (ftColl_80076CBC -> ftColl_80076808(..., type=1, ...) -> lbColl_80008688).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  MSL_LBCOLL_INSERT_FT_SHIELD = 1,
  // Present in the decomp refresh sets; TODO: identify callsites and name precisely.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (case 2)
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820 (case 2)
  MSL_LBCOLL_INSERT_TODO_2 = 2,
  // Hitbox-vs-hitbox "contact" insert (inlineA0/inlineA1 call lbColl_80008688(..., type=3, ...)).
  // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
  MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT = 3,
  // Item-vs-item clank insert (it_8026FE68 uses type=4 when hit->x41_b5 is set).
  // refs/melee/src/melee/it/itcoll.c::it_8026FE68
  MSL_LBCOLL_INSERT_IT_ITEM_CLANK_REFRESH = 4,
  // Present in the decomp refresh sets; TODO: identify callsites and name precisely.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (case 5)
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820 (case 5)
  MSL_LBCOLL_INSERT_TODO_5 = 5,
  // Present in the decomp refresh sets; TODO: identify callsites and name precisely.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (case 7)
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008820 (case 7)
  MSL_LBCOLL_INSERT_TODO_7 = 7,
  // Item-vs-item damage insert (it_802706D0 sets var_r5=8 when hit->x41_b4 is set before it_8026FAC4).
  // refs/melee/src/melee/it/itcoll.c::it_802706D0
  MSL_LBCOLL_INSERT_IT_ITEM_DAMAGE_REFRESH = 8,
} MslLbCollInsertType;

// Victim identity key encoding (1 byte).
// Lower 6 bits store a small slot id (fighter port or item slot). Top 2 bits store kind.
enum {
  MSL_HITLIST_VICTIM_KIND_FIGHTER = 0u,  // fighter port in [0..3]
  MSL_HITLIST_VICTIM_KIND_ITEM = 1u,   // item slot in [0..14] (debug only; identity uses spawn_id)
  MSL_HITLIST_VICTIM_KIND_EMPTY = 3u,  // encoded as 0xFF
};

static inline uint8_t msl_hitlist_victim_pack(uint8_t kind, uint8_t slot) {
  return (uint8_t)((uint8_t)((kind & 0x3u) << 6) | (uint8_t)(slot & 0x3Fu));
}

static inline uint8_t msl_hitlist_victim_kind(uint8_t kind_slot) {
  return (uint8_t)((kind_slot >> 6) & 0x3u);
}

static inline uint8_t msl_hitlist_victim_slot(uint8_t kind_slot) {
  return (uint8_t)(kind_slot & 0x3Fu);
}

static inline uint8_t msl_hitlist_victim_is_empty(uint8_t kind_slot) {
  return kind_slot == 0xFFu ? 1u : 0u;
}

// A single victim list entry (compact analogue to refs/melee/src/melee/lb/types.h::HitVictim).
typedef struct MslHitlistVictimEntry {
  // Identity discriminator:
  // - kind==FIGHTER: slot = fighter port; id16 = fighter instance_id proxy; id32 unused.
  // - kind==ITEM: slot = item slot (debug); id32 = item spawn_id (identity); id16 optional (instance_id).
  uint32_t id32;
  uint16_t id16;
  uint8_t kind_slot;
  // Cooldown timer:
  // - 0: present indefinitely (decomp: HitVictim.x4 == 0)
  // - 1..255: finite cooldown in frames
  uint8_t cd;
} MslHitlistVictimEntry;

// Complete per-hitbox victim lists (analogue to refs/melee/src/melee/lb/types.h::HitCapsule victim lists).
typedef struct MslHitlistCapsule {
  MslHitlistVictimEntry victims_1[MSL_HITLIST_VICTIM_CAP];
  MslHitlistVictimEntry victims_2[MSL_HITLIST_VICTIM_CAP];
  uint8_t ring_1;
  uint8_t ring_2;
  uint8_t _pad0[2];
} MslHitlistCapsule;

#ifdef __cplusplus
}  // extern "C"
#endif
