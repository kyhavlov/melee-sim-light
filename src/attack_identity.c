#include "attack_identity.h"

#include <stdint.h>

#include "attack_id_tables.h"
#include "staling.h"

static inline uint16_t inc_attack_instance_plStale_IncrementAttackInstance(MslBatch* batch, int bi) {
  // Decomp: plStale_IncrementAttackInstance never returns 0; it wraps and skips 0.
  // refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance
  if (batch == NULL) {
    return 0;
  }
  uint16_t before = batch->state.stale_attack_instance_counter[bi];
  if (before == 0) {
    before = 1;
  }
  uint16_t after = (uint16_t)(before + 1u);
  if (after == 0) {
    after = 1;
  }
  batch->state.stale_attack_instance_counter[bi] = after;
  return before;
}

void attack_identity_reset_ft_800890BC(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
  batch->state.attack_id[idx] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.attack_instance[idx] = 0;
  batch->state.attack_identity_last_action_id[idx] = 0xFFFFu;
}

void attack_identity_on_motion_state_change_ft_800890D0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  // Decomp call semantics:
  // - Fighter_ChangeMotionState calls ft_800890D0(fp, new_motion_state->move_id) once on motion-state
  //   entry (i.e., a true action transition).
  //   refs/melee/src/melee/ft/fighter.c (ChangeMotionState path)
  //   refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  //
  // Simulator wiring:
  // - We hook the update to msl_anim_timebase_enter() (our common "enter action" helper), but some
  //   codepaths can legitimately reset the animation timebase without changing action_id.
  // - Guard against those "anim restarts" so we don't incorrectly bump x206C (attack_instance).
  if (batch->state.attack_identity_last_action_id[idx] == action_id) {
    return;
  }
  batch->state.attack_identity_last_action_id[idx] = action_id;

  const uint8_t char_id = batch->state.char_id[idx];
  const uint16_t move_id = attack_id_move_id_from_action(char_id, action_id);

  // Decomp: ft_800890D0 increments x206C when move_id==1 OR move_id != current attackID.
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  const uint16_t cur = batch->state.attack_id[idx];
  if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || move_id != cur) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    batch->state.attack_id[idx] = move_id;
    batch->state.attack_instance[idx] = inc_attack_instance_plStale_IncrementAttackInstance(batch, bi);
  }
}
