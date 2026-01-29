#pragma once

#include <stdint.h>

// Init-time loader for a small subset of "special move" submotion ids (msids) per character.
//
// Source of truth: `data/special_msids/<character>.json` (ISO-derived from fighter animation names).
// Extractor: `tools/extraction/extract_special_msids.py`.
//
// IMPORTANT: special_msids_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslSpecialMsids {
  // Down special (Reflector / Shine) submotions.
  uint16_t speciallw_ground_start;
  uint16_t speciallw_ground_loop;
  uint16_t speciallw_ground_hit;
  uint16_t speciallw_ground_end;
  uint16_t speciallw_air_start;
  uint16_t speciallw_air_loop;
  uint16_t speciallw_air_hit;
  uint16_t speciallw_air_end;

  // Side special (Illusion/Phantasm) submotions.
  uint16_t specials_ground_start;
  uint16_t specials_ground_main;
  uint16_t specials_ground_end;
  uint16_t specials_air_start;
  uint16_t specials_air_main;
  uint16_t specials_air_end;
} MslSpecialMsids;

int special_msids_init(void);
const MslSpecialMsids* msl_special_msids(uint8_t char_id);
