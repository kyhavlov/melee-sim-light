#include "blaster.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "laser_params.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

// Fox/Falco motion states (GALE01) for SpecialN.
//
// Decomp (explicit numeric ids in comments):
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
//   (ftFx_MS_SpecialNStart=341 .. ftFx_MS_SpecialAirNEnd=346)
// - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
//   (Falco uses the same ftFx_* MotionState ids; comments match Fox)
enum {
  MSL_ACT_FX_SPECIAL_N_START = 0x0155,      // ftFx_MS_SpecialNStart
  MSL_ACT_FX_SPECIAL_N_LOOP = 0x0156,       // ftFx_MS_SpecialNLoop
  MSL_ACT_FX_SPECIAL_N_END = 0x0157,        // ftFx_MS_SpecialNEnd
  MSL_ACT_FX_SPECIAL_AIR_N_START = 0x0158,  // ftFx_MS_SpecialAirNStart
  MSL_ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159,   // ftFx_MS_SpecialAirNLoop
  MSL_ACT_FX_SPECIAL_AIR_N_END = 0x015A,    // ftFx_MS_SpecialAirNEnd
};

static inline uint8_t action_is_blaster(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_N_START:
    case MSL_ACT_FX_SPECIAL_N_LOOP:
    case MSL_ACT_FX_SPECIAL_N_END:
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_allows_blaster_entry_ground(uint16_t action_id) {
  // Spotdodge (EscapeN) has an empty IASA in decomp, so it cannot be interrupted into SpecialN.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_IASA
  //
  // Keep this narrowly scoped: other ground states (including shield) have non-empty IASA callbacks
  // and may allow specials depending on per-state input checks.
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_N) {
    return 0;
  }
  return msl_action_is_ground_locomotion(action_id);
}

static inline void enter_wait(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_blaster_start(MslBatch* batch, size_t idx, const MslLaserParams* lp,
                                       uint8_t grounded) {
  if (lp == NULL) {
    return;
  }
  if (grounded) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_START;
    batch->state.animation_index[idx] = (uint32_t)lp->ground_start_msid;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START;
    batch->state.animation_index[idx] = (uint32_t)lp->air_start_msid;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // Decomp: SpecialN enter clears self velocities (gr_vel/self_vel.x/y/z = 0).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter and ::ftFx_SpecialAirN_Enter
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
}

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  // Decomp uses ftAnim_IsFramesRemaining (joint-track remaining) to gate transitions; approximate
  // deterministically by comparing integer frame indices (Slippi `action_frame` is floor(state_age)).
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
  const uint16_t cur = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(anim_frame_f32));
  const uint16_t end_i = msl_anim_frame_floor_u16(end);
  return cur >= end_i;
}

void blaster_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // Entry (minimal): allow from basic locomotion states.
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      if (!action_is_blaster(a) && (pressed & (uint16_t)MSL_BUTTON_B) != 0) {
        if (on_ground) {
          if (action_allows_blaster_entry_ground(a)) {
            enter_blaster_start(batch, idx, lp, 1);
          }
        } else {
          if (msl_action_is_air_locomotion(a)) {
            enter_blaster_start(batch, idx, lp, 0);
          }
        }
      }

      // Update the active SpecialN timeline and handle Start/Loop/End transitions.
      const uint16_t a2 = batch->state.action_id[idx];
      if (!action_is_blaster(a2)) {
        continue;
      }

      const float anim_frame_f32 = batch->state.anim_frame_f32[idx];

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      switch (a2) {
        case MSL_ACT_FX_SPECIAL_N_START:
          if (anim_finished(cid, lp->ground_start_msid, anim_frame_f32)) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_LOOP;
            batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          }
          break;
        case MSL_ACT_FX_SPECIAL_N_LOOP:
          if (anim_finished(cid, lp->ground_loop_msid, anim_frame_f32)) {
            const uint16_t held = batch->state.input_buttons[idx];
            if ((held & (uint16_t)MSL_BUTTON_B) != 0) {
              // Restart loop.
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            } else {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_N_END;
              batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_N_END:
          if (anim_finished(cid, lp->ground_end_msid, anim_frame_f32)) {
            enter_wait(batch, idx);
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_START:
          if (anim_finished(cid, lp->air_start_msid, anim_frame_f32)) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP;
            batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
          if (anim_finished(cid, lp->air_loop_msid, anim_frame_f32)) {
            const uint16_t held = batch->state.input_buttons[idx];
            if ((held & (uint16_t)MSL_BUTTON_B) != 0) {
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            } else {
              batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END;
              batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
              msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_END:
          if (anim_finished(cid, lp->air_end_msid, anim_frame_f32)) {
            if (on_ground) {
              enter_wait(batch, idx);
            } else {
              enter_fall(batch, idx);
            }
          }
          break;
        default:
          break;
      }
    }
  }
}
