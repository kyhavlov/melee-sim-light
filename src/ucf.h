#pragma once

#include <stdint.h>

typedef struct MslStickI8 {
  int8_t x;
  int8_t y;
} MslStickI8;

// Apply Melee-style stick clamp (HSD_PadClampCheck3) using the game's defaults:
// clamp_stickMax=80, clamp_stickMin=0, clamp_stickShift=1.
MslStickI8 ucf_clamp_stick_i8(int8_t raw_x, int8_t raw_y);

// Apply UCF 1.0 cardinals snap (optional).
MslStickI8 ucf_apply_cardinals_1_0_i8(MslStickI8 clamped);

// Full processing used by the sim input pass:
// - If ucf_enabled && ucf_cardinals_1_0_enabled, apply 1.0 cardinals snap using raw axes.
// - Apply Melee clamp to legal coordinates.
MslStickI8 ucf_process_stick_i8(
    int8_t raw_x,
    int8_t raw_y,
    uint8_t ucf_enabled,
    uint8_t ucf_cardinals_1_0_enabled);
