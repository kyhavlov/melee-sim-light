#pragma once

#include <stdint.h>

// HitElement ids (GALE01).
//
// Decomp source of truth:
// - refs/melee/src/melee/lb/forward.h::HitElement
//
// The simulator stores the extracted HitElement in `state.hitbox_element` (low 8 bits of MSLHITB1
// u16_4; see src/hitboxes_tables.h).
enum {
  MSL_HIT_ELEMENT_NORMAL = 0,
  MSL_HIT_ELEMENT_ELECTRIC = 2,
  MSL_HIT_ELEMENT_CATCH = 8,    // HitElement_Catch
  MSL_HIT_ELEMENT_GROUND = 10,  // HitElement_Ground
  MSL_HIT_ELEMENT_INERT = 11,   // HitElement_Inert
};
