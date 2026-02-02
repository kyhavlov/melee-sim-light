#include "anim_timebase.h"

#include <stddef.h>

#include "action_ids.h"
#include "anim_table.h"

static inline void anim_timebase_apply_aobj_loop(MslBatch* batch, size_t idx) {
  // Fighter AObj loop semantics (AOBJ_LOOP) apply a deterministic rewind+wrap when
  // `end_frame <= curr_frame`.
  //
  // Decomp:
  // - Motion-state animation init sets AOBJ_LOOP when fp->x594_b1_loop is set.
  //   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8
  // - AOBJ_LOOP wrap is implemented by sysdolphin's HSD_AObjInterpretAnim.
  //   refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  //
  // We model the wrapped `curr_frame` on the sim's deterministic Q16.16 timebase so that Slippi
  // `state_age`/`action_frame` match on looping locomotion timelines (e.g. Run).
  if (batch == NULL) {
    return;
  }
  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const uint16_t smid = (uint16_t)anim_u32;
  if (!msl_anim_is_looping(batch->state.char_id[idx], smid)) {
    return;
  }
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
  if (!(end_frame > 0.0f)) {
    return;
  }
  const int32_t end_fp = msl_q16_16_from_f32(end_frame);
  if (end_fp <= 0) {
    return;
  }
  int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
  if (cur_fp < 0) {
    return;
  }
  if (cur_fp >= end_fp) {
    // Deterministic modulo wrap. Decomp: HSD_AObjLoadDesc sets rewind_frame=0.0F by default.
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjLoadDesc
    cur_fp = cur_fp % end_fp;
    batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
  }
}

static inline void anim_timebase_apply_capture_loop(MslBatch* batch, size_t idx) {
  // CapturePulled*/CaptureWait*/CaptureDamage* victims use looping HSD AObj timelines in the suite
  // (most notably the CaptureDamage* loop with end_frame=20). Vanilla wraps `cur_anim_frame` under
  // the AObj's loop mode during ftAnim_8006EBA4 / HSD_AObjInterpretAnim.
  //
  // Decomp tie-down: these motion states consult their AObj timebase via fp->cur_anim_frame and
  // read bone world translations during Phys (lb_8000B1CC), so the wrapped timebase affects the
  // per-frame capture delta in ftCo_Attack100.c::fn_800DAD18.
  //
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  if (batch == NULL) {
    return;
  }
  if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[idx])) {
    return;
  }
  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
  if (!(end_frame > 0.0f)) {
    return;
  }
  const int32_t end_fp = msl_q16_16_from_f32(end_frame);
  if (end_fp <= 0) {
    return;
  }
  int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
  if (cur_fp < 0) {
    return;
  }
  if (cur_fp >= end_fp) {
    // Deterministic modulo wrap (Q16.16), matching the observed Slippi `state_age` wrap behavior on
    // looping capture timelines.
    cur_fp = cur_fp % end_fp;
    batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
  }
}

void anim_timebase_update_pre_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Hitlag freezes animation advancement (decomp gate is fp->x2219_b5).
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      if (batch->state.hitlag_started_frame[idx] != 0) {
        // Keep derived fields coherent even when frozen.
        msl_anim_timebase_recompute_derived(batch, idx);
        continue;
      }

      batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
      anim_timebase_apply_aobj_loop(batch, idx);
      anim_timebase_apply_capture_loop(batch, idx);
      msl_anim_timebase_recompute_derived(batch, idx);
    }
  }
}
