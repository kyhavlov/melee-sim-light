#include "motion_state_runtime.h"

#include "motion_state_owners.h"
#include "hitboxes.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_floor_skip.h"

void motion_state_install_live_callbacks(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t char_id = batch->state.char_id[idx];
  const uint16_t action_id = batch->state.action_id[idx];
  batch->state.live_coll_callback_id[idx] = msl_motion_state_coll_cb_id(char_id, action_id);
  batch->state.live_coll_handler_kind[idx] = msl_motion_state_coll_handler_kind(char_id, action_id);
}

void motion_state_finalize_seeded_coll_data_before_map(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.live_coll_handler_kind[idx] == (uint8_t)MSL_COLL_HANDLER_LEGACY ||
          batch->state.floor_sweep_prev_source_owned[idx] == 0u ||
          batch->state.floor_sweep_prev_runtime_owned[idx] != 0u) {
        continue;
      }
      // One-step seeds expose CollData.last_pos but not the persistent ECB packet. IASA may also
      // install a destination after reseed, so initialize the real hidden packet once after the
      // final pre-map motion entry. Free-running lanes carry runtime-owned CollData and never take
      // this initialization path.
      // data/motion_state/owners/*.bin::MSLMSO01 coll_handler_kind
      // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_procMap}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
      const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      MslEcbWorldPoints ecb = {0};
      msl_ecb_world_points_sample(&ecb, batch->state.char_id[idx],
                                  batch->state.animation_index[idx], msl_ecb_prev_frame_u16(frame),
                                  batch->state.facing[idx] ? 1.0f : -1.0f, 0.0f, 0.0f, 1u);
      mpcoll_store_current_ecb_points(batch, idx, &ecb);
      mpcoll_store_prev_ecb_points(batch, idx, &ecb);
      batch->state.coll_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
    }
  }
}

void motion_state_override_live_coll_callback(MslBatch* batch, size_t idx, uint16_t callback_id,
                                              uint8_t handler_kind) {
  if (batch == NULL) {
    return;
  }
  batch->state.live_coll_callback_id[idx] = callback_id;
  batch->state.live_coll_handler_kind[idx] = handler_kind;
}

void motion_state_change(MslBatch* batch, int bi, int p, uint16_t action_id,
                         uint32_t animation_index, uint32_t flags, float anim_start,
                         float anim_speed, MslAnimEnterTickPolicy tick_policy) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t old_fastfall = batch->state.fall_fast[idx];
  if ((flags & (uint32_t)MSL_MOTION_ENTRY_SKIP_HIT) == 0u) {
    hitboxes_clear_player_active(batch, bi, p);
  }
  // Fighter_ChangeMotionState clears CollData.floor_skip before installing the destination row.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/mp/mpcoll.c::mpClearFloorSkip
  msl_mpcoll_clear_floor_skip(batch, idx);
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = animation_index;
  msl_anim_timebase_enter_with_policy(batch, idx, anim_start, anim_speed, tick_policy);
  batch->state.fall_fast[idx] =
      (flags & (uint32_t)MSL_MOTION_ENTRY_KEEP_FASTFALL) != 0u ? old_fastfall : 0u;
}
