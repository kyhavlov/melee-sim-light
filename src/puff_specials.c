#include "puff_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "ids.h"
#include "locomotion.h"

// ---------------------------------------------------------------------------
// Multi-jump ladder (ftPr_MS_JumpAerialF1..F5)
// ---------------------------------------------------------------------------

static inline uint8_t pr_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(anim_frame_f32) >= end) ? 1u : 0u;
}

void puff_mjump_turn_tick(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
  if (batch == NULL || batch->state.puff_mjump_turn_timer[idx] == 0u) {
    return;
  }
  batch->state.puff_mjump_turn_timer[idx]--;
  if (ch != NULL && ch->puff_mjump_turn_frames > 0 &&
      batch->state.puff_mjump_turn_timer[idx] == (uint8_t)(ch->puff_mjump_turn_frames / 2)) {
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
  }
}

void puff_specials_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u || batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];
      const uint16_t a = batch->state.action_id[idx];
      if (!puff_action_is_multijump(cid, a)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params_fast(cid);
      if (ch == NULL) {
        continue;
      }

      // ftCo_JumpAerialF1_Anim runs ft_800CB6EC every frame. The entry itself
      // (ftCo_800D74A4) already applied one tick, so skip the tick on entry rows
      // (prev_action_id != a covers both live entries earlier this action phase and
      // teacher-forced seed rows whose source entry tick already happened pre-serialize).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D74A4,
      //   ftCo_JumpAerialF1_Anim}
      if (batch->state.prev_action_id[idx] == a) {
        puff_mjump_turn_tick(batch, ch, idx);
      }

      // Anim end: jumps exhausted -> FallAerial (Ft_MF_None clears fastfall), else Fall.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_JumpAerialF1_Anim
      const uint16_t msid = puff_special_submotion(a);
      if (pr_anim_finished(cid, msid, batch->state.anim_frame_f32[idx])) {
        if (batch->state.jumps_left[idx] == 0u) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_AERIAL;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_AERIAL;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          batch->state.fall_fast[idx] = 0u;
        } else {
          msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
        }
        // The destination Fall/FallAerial IASA runs in the same Fighter_procUpdate; a held
        // chain input can consume the next ladder jump immediately (the tail's jump entry
        // forks back into the multi-jump admission for this char).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_IASA
        (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
        continue;
      }

      // ftCo_JumpAerial_IASA (shared by the ladder states): aerial attacks, EscapeAir, and the
      // jump chain. B rows are left unconsumed for the future puff specials owner; the jump
      // entry inside the tail routes through the multi-jump admission (chain window gated on
      // the script cmd0 pulse inside locomotion's fork).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
      if (batch->state.hitstun[idx] == 0u &&
          (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
        (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
      }
    }
  }
}

void puff_specials_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(batch_index, p);
    if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
      continue;
    }
    // The turnaround window counter (mv.co.jumpaerial.x0) is hidden per-action state the replay
    // does not carry. Reseed starts with no armed window: a reversed-jump seed row inside the
    // ~turn_frames entry window will miss the pending mid-window facing flip (documented
    // approximation; the flip itself is replay-visible one row later and self-corrects).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
    batch->state.puff_mjump_turn_timer[idx] = 0u;
  }
}
