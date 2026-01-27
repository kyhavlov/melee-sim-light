#include "staling.h"

#include "staling_tables.h"

uint16_t staling_move_id_from_state(const MslBatch* batch, size_t fighter_idx) {
  if (batch == NULL) {
    return 0xFFFFu;
  }
  const uint8_t char_id = batch->state.char_id[fighter_idx];
  const uint32_t msid_u32 = batch->state.animation_index[fighter_idx];
  if (msid_u32 > 0xFFFFu) {
    return 0xFFFFu;
  }
  const uint16_t msid = (uint16_t)msid_u32;
  return staling_move_id_from_msid(char_id, msid);
}

float staling_multiplier_for_move(const MslBatch* batch, size_t fighter_idx, uint16_t move_id) {
  if (batch == NULL) {
    return 1.0f;
  }
  // Decomp: ft_80089118 returns 1.0 immediately for move_id==1 (FtMoveId_Default).
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  if (move_id == 0xFFFFu || move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 1.0f;
  }
  const float* weights = staling_weights_table();
  if (weights == NULL) {
    return 1.0f;
  }

  // Decomp: ft_80089118 consults the previous 9 entries starting from (current_index-1).
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  const uint8_t qi_raw = batch->state.stale_queue_index[fighter_idx];
  const uint8_t qi = (qi_raw < (uint8_t)MSL_STALE_QUEUE_SIZE) ? qi_raw : 0;
  int pos = (qi != 0) ? (int)qi - 1 : (int)MSL_STALE_QUEUE_SIZE - 1;

  const size_t base = fighter_idx * (size_t)MSL_STALE_QUEUE_SIZE;
  float mult = 1.0f;
  for (int i = 0; i < 9; i++) {
    const uint16_t mid = batch->state.stale_move_id[base + (size_t)pos];
    // Decomp: encountering a 0 move_id entry terminates the scan immediately.
    // refs/melee/src/melee/ft/ft_0881.c::ft_80089118 (`if (table->StaleMoves[var_r8].move_id == 0) return var_f1;`)
    if (mid == 0) {
      return mult;
    }
    if (mid == move_id) {
      mult -= weights[i];
    }
    pos = (pos != 0) ? (pos - 1) : ((int)MSL_STALE_QUEUE_SIZE - 1);
  }
  // Decomp: ft_80089118 returns the computed multiplier directly (no clamp).
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  return mult;
}

void staling_queue_update(MslBatch* batch, size_t fighter_idx, uint16_t move_id,
                          uint16_t attack_instance) {
  if (batch == NULL) {
    return;
  }
  if (move_id == 0xFFFFu || move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return;
  }
  // Decomp: plStale_IncrementAttackInstance never returns 0; 0 is used for cleared/empty entries.
  // refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance and ::plStale_ResetStaleMoveTableForPlayer
  if (attack_instance == 0) {
    return;
  }

  const size_t base = fighter_idx * (size_t)MSL_STALE_QUEUE_SIZE;

  // Duplicate suppression: ignore if the exact (move_id, attack_instance) pair already exists.
  // Decomp: plStale_UpdateStaleMovesFromFighter / ...FromItem
  // refs/melee/src/melee/pl/plstale.c
  for (int i = 0; i < MSL_STALE_QUEUE_SIZE; i++) {
    if (batch->state.stale_move_id[base + (size_t)i] == move_id &&
        batch->state.stale_attack_instance[base + (size_t)i] == attack_instance) {
      return;
    }
  }

  uint8_t qi = batch->state.stale_queue_index[fighter_idx];
  if (qi >= (uint8_t)MSL_STALE_QUEUE_SIZE) {
    qi = 0;
  }

  batch->state.stale_move_id[base + (size_t)qi] = move_id;
  batch->state.stale_attack_instance[base + (size_t)qi] = attack_instance;
  qi = (qi == (uint8_t)MSL_STALE_QUEUE_SIZE - 1) ? 0 : (uint8_t)(qi + 1);
  batch->state.stale_queue_index[fighter_idx] = qi;
}
