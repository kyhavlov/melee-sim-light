#pragma once

#include <stdint.h>

#include "buttons.h"

static inline float msl_trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float msl_trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return msl_trigger_u8_to_unit(m);
}

