#include "grab_flow.h"

#include "action_ids.h"
#include "anim_timebase.h"
#include "move_tables.h"

static inline void enter_catch_wait_from_pull(MslBatch* batch, size_t oidx) {
  // Decomp: CatchPull_Anim enters CatchWait via fn_800DA1D8 (Fighter_ChangeMotionState to 0xD8).
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA1D8
  //
  // Slippi post-frame `animation_index` is GALE01 ftCommon_AnimID. In decomp forward.h, the Catch*
  // submotions are contiguous (Catch, CatchDash, CatchWait, ...), so we derive CatchWait's msid by
  // a small constant offset from the current pull animation.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_SM_Catch*, contiguous enum entries)
  const uint16_t a = batch->state.action_id[oidx];
  if (a == (uint16_t)MSL_ACT_CATCH_PULL) {
    batch->state.animation_index[oidx] = batch->state.animation_index[oidx] + 2u;
  } else if (a == (uint16_t)MSL_ACT_CATCH_DASH_PULL) {
    batch->state.animation_index[oidx] = batch->state.animation_index[oidx] + 1u;
  }
  batch->state.action_id[oidx] = (uint16_t)MSL_ACT_CATCH_WAIT;
  msl_anim_timebase_enter(batch, oidx, 0.0f, 1.0f);
}

static inline void enter_capture_wait_from_pulled(MslBatch* batch, size_t vidx) {
  // Decomp: CatchPull->CatchWait entry calls fn_800DB6C8 on the victim gobj, which enters CaptureWait
  // (hi/lw) based on the current capture variant.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DA1D8
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DB6C8
  const uint16_t a = batch->state.action_id[vidx];
  if (a == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_HI;
  } else if (a == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW) {
    batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
  } else {
    return;
  }

  // ftCommon_AnimID contiguous enum: CapturePulled* -> CaptureWait* is the next msid.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_SM_CapturePulled*/Wait*, contiguous)
  batch->state.animation_index[vidx] = batch->state.animation_index[vidx] + 1u;
  msl_anim_timebase_enter(batch, vidx, 0.0f, 1.0f);
}

void grab_flow_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int owner_p = 0; owner_p < num_players; owner_p++) {
      const size_t oidx = msl_idx_player(bi, owner_p);
      if (batch->state.hitlag_started_frame[oidx] != 0) {
        continue;
      }

      const uint16_t oa = batch->state.action_id[oidx];
      if (oa != (uint16_t)MSL_ACT_CATCH_PULL && oa != (uint16_t)MSL_ACT_CATCH_DASH_PULL) {
        continue;
      }

      if (!move_tables_catchpull_should_enter_wait(batch->state.char_id[oidx], oa,
                                                   batch->state.anim_frame_f32[oidx])) {
        continue;
      }

      enter_catch_wait_from_pull(batch, oidx);

      // Sync victim motion state (CapturePulled* -> CaptureWait*) for all victims owned by this grabber.
      for (int victim_p = 0; victim_p < num_players; victim_p++) {
        const size_t vidx = msl_idx_player(bi, victim_p);
        if (batch->state.hitlag_started_frame[vidx] != 0) {
          continue;
        }
        if ((int)batch->state.grab_owner_port[vidx] != owner_p) {
          continue;
        }
        enter_capture_wait_from_pulled(batch, vidx);
      }
    }
  }
}
