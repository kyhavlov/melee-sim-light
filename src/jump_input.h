#pragma once

#include <stdint.h>

// ftCo_JumpInput (refs/melee/src/melee/ft/chara/ftCommon/forward.h).
typedef enum MslJumpInput {
  MSL_JUMP_INPUT_NONE = 0,
  MSL_JUMP_INPUT_LSTICK = 1,
  MSL_JUMP_INPUT_CSTICK = 2,
  MSL_JUMP_INPUT_XY = 3,
} MslJumpInput;
