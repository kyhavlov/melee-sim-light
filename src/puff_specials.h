#pragma once

#include <stddef.h>
#include <stdint.h>

#include "char_params.h"
#include "state.h"

// Jigglypuff char-special module. Current scope: the multi-jump ladder
// (ftPr_MS_JumpAerialF1..F5, actions 341..345 driven by the common ftCo_JumpAerialF1_*
// callbacks and the fp->x2D0 stats block). Entry lives in locomotion.c (the ladder is jump
// locomotion and reuses the static air-jump entry bookkeeping); this module owns the
// per-frame Anim/IASA work, the reseed reconstruction, and (via physics.c branches) the
// scaled ladder drift. Specials (Rollout/Pound/Sing/Rest) land here in later phases.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D730C,ftCo_800D74A4,
//   ftCo_JumpAerialF1_Anim,ftCo_JumpAerialF1_Phys}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{ft_800CB6EC,ftCo_800CBAC4}

// Submotion mapping for the puff char-range block: uniformly action - 46 (295..326).
static inline uint16_t puff_special_submotion(uint16_t action_id) {
  return (uint16_t)(action_id - 46u);
}

static inline uint8_t puff_action_is_multijump(uint8_t char_id, uint16_t action_id) {
  return (uint8_t)(char_id == 15u /* MSL_CHAR_ID_PUFF */ && action_id >= 341u &&
                   action_id <= 345u);
}

// One ft_800CB6EC turnaround-window tick: decrement the armed counter; the scalar facing flips
// when the countdown reaches turn_frames/2 (the model rotation itself is cosmetic).
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
void puff_mjump_turn_tick(MslBatch* batch, const MslCharParams* ch, size_t idx);

// Per-frame Anim-callback work for the ladder states (turnaround window tick + anim-end
// exits) plus the ladder's aerial IASA chain. Runs with the other char-special modules in
// the action phase.
void puff_specials_update_pre_physics(MslBatch* batch);

// Reseed reconstruction for hidden ladder state (the turnaround window counter).
void puff_specials_reseed_init(MslBatch* batch, int batch_index);
