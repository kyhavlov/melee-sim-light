#pragma once

#include <stdint.h>

// Minimal per-character physics parameters (temporary).
//
// Source of truth: ISO-extracted character attributes under `data/characters/*.json`.
// - Fox:   `data/characters/fox.json`   keys `grav`, `terminal_vel`
// - Falco: `data/characters/falco.json` keys `grav`, `terminal_vel`
//
// Note: this is intentionally small; we will replace/expand it when the extracted data
// is loaded into native tables.

typedef struct MslCharPhysicsParams {
  float grav;
  float terminal_vel;
} MslCharPhysicsParams;

static inline MslCharPhysicsParams msl_char_physics_params(uint8_t char_id) {
  // Character id mapping follows Slippi post-frame `character` (GALE01).
  // - Fox   = 1
  // - Falco = 22
  switch (char_id) {
    case 1:  // Fox
      return (MslCharPhysicsParams){.grav = 0.23f, .terminal_vel = 2.80f};
    case 22:  // Falco
      return (MslCharPhysicsParams){.grav = 0.17f, .terminal_vel = 3.10f};
    default:
      // Fallback for unsupported characters; keep deterministic.
      return (MslCharPhysicsParams){.grav = 0.20f, .terminal_vel = 3.00f};
  }
}
