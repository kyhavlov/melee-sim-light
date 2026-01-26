#pragma once

// Math constants used in decomp-first gameplay logic.
//
// Pi source-of-truth (GALE01):
// - refs/melee/src/MSL/math.h (M_PI)
// - used by ftFx_SpecialN_CreateBlasterShot for angle mirroring (M_PI - angle)
//   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
#define MSL_PI_F 3.14159265358979323846f

