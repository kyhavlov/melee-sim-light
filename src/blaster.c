#include "blaster.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "laser_params.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

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

static inline uint8_t specialn_is_blaster_loop_requested(const MslBatch* batch, size_t idx) {
  // Decomp (GALE01): the SpecialN Start/Loop IASA callbacks set fp->mv.fx.SpecialN.isBlasterLoop
  // when:
  //   fp->cmd_vars[0] != 0 && (fp->input.x668 & HSD_PAD_B)
  // where fp->input.x668 is the per-frame pressed mask (rising edge).
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNStart_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_IASA
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_IASA
  //
  // This simulator does not yet model cmd_vars[0] or mv.fx.SpecialN.isBlasterLoop directly.
  // However, the seed schema includes fp->x67D ("frames since last B press", saturating at 0xFF)
  // and action_frame (derived from cur_anim_frame). Use the decomp-shaped condition:
  // "B was pressed at some point since this motion state was entered".
  if (batch == NULL) {
    return 0;
  }
  const int af = (int)batch->state.action_frame[idx];
  if (af < 0) {
    return 0;
  }
  const int x67d = (int)batch->state.x67D[idx];
  if (x67d < 0 || x67d > 255) {
    return 0;
  }
  if (x67d == 0xFF) {
    return 0;
  }
  return x67d <= af ? 1u : 0u;
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
  return msl_anim_frame_sanitize_f32(anim_frame_f32) >= end;
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
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
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
          // Landing special-case: Landing lag actions should not be interruptible until their IASA
          // gate allows it.
          //
          // Decomp:
          // - Normal Landing IASA returns early while fp->cur_anim_frame < fp->co_attrs.normal_landing_lag,
          //   then checks specials including SpecialN (ftCo_800D6824).
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D6824
          // - LandingFallSpecial sets allow_interrupt=false, so the same IASA path blocks all interrupts.
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter_Basic
          // - LandingAir* IASA is empty, so those landing-lag actions cannot be interrupted at all.
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_IASA
          uint8_t allow = action_allows_blaster_entry_ground(a);
          if (allow) {
            switch (a) {
              case (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL:
              case (uint16_t)MSL_ACT_LANDING_AIR_N:
              case (uint16_t)MSL_ACT_LANDING_AIR_F:
              case (uint16_t)MSL_ACT_LANDING_AIR_B:
              case (uint16_t)MSL_ACT_LANDING_AIR_HI:
              case (uint16_t)MSL_ACT_LANDING_AIR_LW:
                allow = 0;
                break;
              case (uint16_t)MSL_ACT_LANDING: {
                // `anim_timebase_update_pre_input()` runs before input processing, matching decomp
                // prio 1 (Anim) before prio 3 (Input). Use post-advance cur_anim_frame here so the
                // special becomes available on the correct actionable frame.
                const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
                allow = (cur >= (float)ch->landing_lag_frames) ? 1u : 0u;
              } break;
              default:
                break;
            }
          }
          if (allow) {
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

      // Ensure SpecialN/SpecialAirN always has a valid msid-backed animation_index.
      // This keeps the ECB/collision substrate stable under reseed and removes the need for
      // teacher-forcing guards in post-collision landing logic.
      switch (a2) {
        case MSL_ACT_FX_SPECIAL_N_START:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_start_msid;
          break;
        case MSL_ACT_FX_SPECIAL_N_LOOP:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_loop_msid;
          break;
        case MSL_ACT_FX_SPECIAL_N_END:
          batch->state.animation_index[idx] = (uint32_t)lp->ground_end_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_START:
          batch->state.animation_index[idx] = (uint32_t)lp->air_start_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
          batch->state.animation_index[idx] = (uint32_t)lp->air_loop_msid;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_N_END:
          batch->state.animation_index[idx] = (uint32_t)lp->air_end_msid;
          break;
        default:
          break;
      }

      const float anim_frame_f32 = batch->state.anim_frame_f32[idx];

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (anim gate)
      if (batch->state.hitlag_started_frame[idx] != 0) {
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
            if (specialn_is_blaster_loop_requested(batch, idx)) {
              // Loop -> Loop: request another shot cycle.
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
            if (specialn_is_blaster_loop_requested(batch, idx)) {
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

void blaster_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }

      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }

      const uint8_t was_ground = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t now_ground = batch->state.on_ground[idx] ? 1u : 0u;
      if (was_ground || !now_ground) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      if (a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START &&
          a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP &&
          a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END) {
        continue;
      }

      // If the seed doesn't provide a valid animation_index, the ECB/collision substrate may snap
      // in unrealistic ways. We enforce a valid msid-backed animation_index in blaster_update_pre_physics,
      // so post-collision landing can be driven purely by ground contact.

      // Landing transition for aerial SpecialN.
      //
      // Decomp:
      // - ftFx_SpecialAirN*_Coll uses ftCo_AirCatchHit_Coll, which enters Landing_Enter_Basic.
      //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
      //   refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
      //
      // Policy:
      // - Use plain Landing (not LandingAir*): this matches Landing_Enter_Basic.
      // - Preserve horizontal momentum by transferring air X -> ground X; clear air X.
      // - Refresh jumps on grounded transition (fp->x1968_jumpsUsed = 0).
      //   refs/melee/src/melee/ft/ftcommon.c:556-573
      batch->state.speed_ground_x_self[idx] = batch->state.speed_air_x_self[idx];
      batch->state.speed_air_x_self[idx] = 0.0f;
      batch->state.fall_fast[idx] = 0;
      batch->state.jumps_left[idx] = ch->max_jumps;

      batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
      msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    }
  }
}
