#include "grab_flow.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "move_tables.h"

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  // Decomp gates on "frames remaining" (joint track remaining). In this sim we approximate via a
  // simple end-frame comparison on the sanitized float timebase.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
}

static inline void enter_wait_from_catch_end(MslBatch* batch, size_t idx) {
  // Catch/CatchDash Anim end -> Wait.
  //
  // Decomp:
  // - ftCo_Catch_Anim: if !ftAnim_IsFramesRemaining, call ft_8008A2BC.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Catch_Anim
  // - ftCo_CatchDash_Anim: similar end gate, then ft_8008A2BC.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CatchDash_Anim
  // - ft_8008A2BC usually routes to ft_8008A348 -> Fighter_ChangeMotionState(ftCo_MS_Wait).
  //   refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

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

      // Catch/CatchDash Anim end -> Wait should happen before guard entry checks later in this
      // frame, so buffered shield inputs become active immediately on the first actionable frame
      // after a whiffed grab.
      //
      // Decomp: ftCo_Catch_IASA and ftCo_CatchDash_IASA are empty. On anim end, Catch/CatchDash
      // transition via ft_8008A2BC to a neutral state (usually ftCo_MS_Wait), and then the normal
      // grounded interrupt checks (including shield via ftCo_80091A4C) can run later.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_CatchDash_Anim}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_IASA,ftCo_CatchDash_IASA}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
      if (oa == (uint16_t)MSL_ACT_CATCH || oa == (uint16_t)MSL_ACT_CATCH_DASH) {
        const uint32_t msid_u32 = batch->state.animation_index[oidx];
        if (msid_u32 <= 0xFFFFu) {
          const uint16_t msid = (uint16_t)msid_u32;
          if (anim_finished(batch->state.char_id[oidx], msid, batch->state.anim_frame_f32[oidx])) {
            enter_wait_from_catch_end(batch, oidx);
          }
        }
      }

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
