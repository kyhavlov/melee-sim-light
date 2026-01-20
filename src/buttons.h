#pragma once

#include <stdint.h>

// Digital button bitmask mapping for `MslInputPlayer.buttons`.
//
// Source of truth:
// - Slippi `buttons_physical` (tools/slippi/make_dataset_from_slp.py)
// - Dolphin engine-dump capture uses the same stable mapping:
//   tools/dolphin/engine_dump_capture.py
//
// These match Melee/HSD PAD bit positions (GALE01).

enum {
  MSL_BUTTON_A = 0x0100,
  MSL_BUTTON_B = 0x0200,
  MSL_BUTTON_X = 0x0400,
  MSL_BUTTON_Y = 0x0800,
  MSL_BUTTON_Z = 0x0010,
  MSL_BUTTON_L = 0x0040,
  MSL_BUTTON_R = 0x0020,
  MSL_BUTTON_START = 0x1000,
  MSL_BUTTON_D_UP = 0x0008,

  MSL_BUTTON_XY = 0x0400 | 0x0800,
};
