#pragma once

#include <stdint.h>

// Collision environment flag bits (`CollData.env_flags`) as used by mpColl and fighter cliff logic.
//
// Source of truth (decomp):
// - refs/melee/src/common_structs.h (Collide_* bit values)
// - refs/melee/src/melee/lb/types.h::CollData (env_flags / prev_env_flags fields)
//
// Ledge usage:
// - refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298 (tests Collide_LedgeGrabMask)
// - refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370 (tests Collide_LeftLedgeGrab)
// - refs/melee/src/melee/mp/mpcoll.c (sets Collide_* bits during stage collision)

enum {
  // Floor contact.
  // refs/melee/src/common_structs.h
  MSL_COLLIDE_FLOOR_PUSH = 0x00008000u,  // Collide_FloorPush
  MSL_COLLIDE_FLOOR_HUG = 0x00010000u,   // Collide_FloorHug
  MSL_COLLIDE_FLOOR_MASK = (0x00008000u | 0x00010000u),

  // Floor edge proximity (used as a ledge-grab suppression gate inside mpColl).
  // refs/melee/src/common_structs.h
  MSL_COLLIDE_LEFT_EDGE = 0x00100000u,   // Collide_LeftEdge
  MSL_COLLIDE_RIGHT_EDGE = 0x00200000u,  // Collide_RightEdge
  MSL_COLLIDE_EDGE = 0x00800000u,        // Collide_Edge

  // Ledge grab candidates (mpColl computes these when airborne and descending).
  // refs/melee/src/common_structs.h
  MSL_COLLIDE_LEFT_LEDGE_GRAB = 0x01000000u,   // Collide_LeftLedgeGrab
  MSL_COLLIDE_RIGHT_LEDGE_GRAB = 0x02000000u,  // Collide_RightLedgeGrab
  MSL_COLLIDE_LEDGE_GRAB_MASK = (0x01000000u | 0x02000000u),
};

