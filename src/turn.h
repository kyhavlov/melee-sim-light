#pragma once

#include <stdint.h>

#include "char_params.h"

static inline uint8_t msl_turn_basic_frames_to_turn_for_entry(const MslCharParams* ch) {
  if (ch == NULL) {
    return 0u;
  }
  // Decomp: ftCo_Turn_Enter_Basic stores co_attrs.frames_to_change_direction_on_standing_turn
  // before Fighter_ChangeMotionState and the immediate ftAnim_8006EBA4 entry tick. The Anim
  // callback countdown starts on the next fighter proc, so the first visible Turn frame still
  // carries the full source frame count. The sim's shared grounded IASA tails can enter Turn in a
  // frame that later reaches the Turn owner pass; bias the stored countdown by one so the first
  // live post-entry Turn row matches ftCo_Turn_Anim_Inner's deferred countdown.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
  //   ftCo_Turn_Enter_Basic,ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
  if (ch->turn_frames >= 0xFFu) {
    return 0xFFu;
  }
  return (uint8_t)(ch->turn_frames + 1u);
}
