#pragma once

#include <stdint.h>

// Immutable runtime config for a batch instance.
// Keep this small and stable; do not include file paths or large tables.
typedef struct MslConfig {
  uint8_t num_players; // 2 or 4

  // Gameplay toggles (defaults: on for modern replays).
  uint8_t ucf_enabled;              // 0/1
  uint8_t ucf_cardinals_1_0_enabled; // 0/1 (more recent / optional)

  uint8_t _pad0[1];
} MslConfig;

void msl_config_default(MslConfig* out, int num_players);

