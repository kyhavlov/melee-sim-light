#include "shine.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "special_msids.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

// Fox/Falco motion states (GALE01) for SpecialLw.
//
// Decomp (explicit numeric ids in comments):
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
//   (ftFx_MS_SpecialLwStart=360 .. ftFx_MS_SpecialAirLwTurn=369)
// - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
//   (Falco uses the same ftFx_* MotionState ids; comments match Fox)
enum {
  MSL_ACT_FX_SPECIAL_LW_START = 0x0168,      // ftFx_MS_SpecialLwStart
  MSL_ACT_FX_SPECIAL_LW_LOOP = 0x0169,       // ftFx_MS_SpecialLwLoop
  MSL_ACT_FX_SPECIAL_LW_HIT = 0x016A,        // ftFx_MS_SpecialLwHit
  MSL_ACT_FX_SPECIAL_LW_END = 0x016B,        // ftFx_MS_SpecialLwEnd
  MSL_ACT_FX_SPECIAL_LW_TURN = 0x016C,       // ftFx_MS_SpecialLwTurn
  MSL_ACT_FX_SPECIAL_AIR_LW_START = 0x016D,  // ftFx_MS_SpecialAirLwStart
  MSL_ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E,   // ftFx_MS_SpecialAirLwLoop
  MSL_ACT_FX_SPECIAL_AIR_LW_HIT = 0x016F,    // ftFx_MS_SpecialAirLwHit
  MSL_ACT_FX_SPECIAL_AIR_LW_END = 0x0170,    // ftFx_MS_SpecialAirLwEnd
  MSL_ACT_FX_SPECIAL_AIR_LW_TURN = 0x0171,   // ftFx_MS_SpecialAirLwTurn
};

// Decomp: in ftFx_Init.c, the aerial SpecialAirLw* motion states are a contiguous block following
// the grounded SpecialLw* block (Start..Turn).
enum { MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA = 5 };

static inline uint8_t action_is_shine(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_LW_START:
    case MSL_ACT_FX_SPECIAL_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_LW_HIT:
    case MSL_ACT_FX_SPECIAL_LW_END:
    case MSL_ACT_FX_SPECIAL_LW_TURN:
    case MSL_ACT_FX_SPECIAL_AIR_LW_START:
    case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
    case MSL_ACT_FX_SPECIAL_AIR_LW_END:
    case MSL_ACT_FX_SPECIAL_AIR_LW_TURN:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_shine_ground(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t action_is_shine_air(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
          action_id <= (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN)
             ? 1
             : 0;
}

static inline uint8_t action_allows_shine_entry_ground(uint16_t action_id) {
  // Keep this narrowly scoped: only enter from basic grounded locomotion states.
  return msl_action_is_ground_locomotion(action_id);
}

static inline uint8_t action_allows_shine_entry_air(uint16_t action_id) {
  return msl_action_is_air_locomotion(action_id);
}

static inline uint8_t anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  if (!(end > 0.0f)) {
    return 0;
  }
  const uint16_t cur = msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(anim_frame_f32));
  const uint16_t end_i = msl_anim_frame_floor_u16(end);
  return cur >= end_i;
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

static inline uint8_t stick_wants_speciallw(const MslCommonParams* c, int8_t main_y) {
  if (c == NULL) {
    return 0;
  }
  const float y = apply_deadzone(stick_i8_to_unit(main_y), c->lstick_deadzone_y);
  return (y <= -c->special_stick_y_threshold) ? 1u : 0u;
}

static inline uint8_t stick_wants_turn(const MslCommonParams* c, int8_t main_x, uint8_t facing) {
  if (c == NULL) {
    return 0;
  }
  const float x = apply_deadzone(stick_i8_to_unit(main_x), c->lstick_deadzone_x);
  const float facing_dir = facing ? 1.0f : -1.0f;
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_800C97A8
  return (x * facing_dir <= c->turn_stick_x_threshold) ? 1u : 0u;
}

static inline void enter_shine_ground_start(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_START;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_air_start(MslBatch* batch, size_t idx, const MslCharParams* ch,
                                        const MslSpecialMsids* ms) {
  if (ch != NULL) {
    // Decomp: ftFx_SpecialAirLw_Enter sets self_vel.y=0 and divides self_vel.x by momentum preserve.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    batch->state.speed_y_self[idx] = 0.0f;
    if (ch->reflector_momentum_preserve_x != 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->reflector_momentum_preserve_x;
    }
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_ground_loop(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_LOOP;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_air_loop(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_ground_end(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_END;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_air_end(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_shine_ground_turn(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  // Decomp: ftFx_SpecialLwTurn_Check enters the turn motion state and flips facing on entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwTurn_Check
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;  // Turn uses Loop msid.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
}

static inline void enter_shine_air_turn(MslBatch* batch, size_t idx, const MslSpecialMsids* ms) {
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN;
  batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;  // Turn uses Loop msid.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
}

void shine_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(cid);
      const MslSpecialMsids* ms = msl_special_msids(cid);
      if (c == NULL || ch == NULL || ms == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // Entry (minimal): B press + down stick from basic locomotion.
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      if (!action_is_shine(a) && (pressed & (uint16_t)MSL_BUTTON_B) != 0 &&
          stick_wants_speciallw(c, batch->state.input_main_y[idx])) {
        if (on_ground) {
          if (action_allows_shine_entry_ground(a)) {
            enter_shine_ground_start(batch, idx, ms);
          }
        } else {
          if (action_allows_shine_entry_air(a)) {
            enter_shine_air_start(batch, idx, ch, ms);
          }
        }
      }

      const uint16_t a2 = batch->state.action_id[idx];
      if (!action_is_shine(a2)) {
        continue;
      }

      // Keep animation_index stable for shine states (avoid seed carry-through).
      switch (a2) {
        case MSL_ACT_FX_SPECIAL_LW_START:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
          break;
        case MSL_ACT_FX_SPECIAL_LW_LOOP:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
          break;
        case MSL_ACT_FX_SPECIAL_LW_HIT:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_hit;
          break;
        case MSL_ACT_FX_SPECIAL_LW_END:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
          break;
        case MSL_ACT_FX_SPECIAL_LW_TURN:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_START:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_hit;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_END:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_TURN:
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
          break;
        default:
          break;
      }

      // Decomp: hitlag freezes animation advancement and blocks Anim/IASA side effects.
      if (batch->state.hitlag[idx] != 0) {
        continue;
      }

      const float anim_frame_f32 = batch->state.anim_frame_f32[idx];
      const uint16_t held = batch->state.input_buttons[idx];

      switch (a2) {
        case MSL_ACT_FX_SPECIAL_LW_START:
          if (anim_finished(cid, ms->speciallw_ground_start, anim_frame_f32)) {
            if (on_ground) {
              enter_shine_ground_loop(batch, idx, ms);
            } else {
              enter_shine_air_loop(batch, idx, ms);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_START:
          if (anim_finished(cid, ms->speciallw_air_start, anim_frame_f32)) {
            if (on_ground) {
              enter_shine_ground_loop(batch, idx, ms);
            } else {
              enter_shine_air_loop(batch, idx, ms);
            }
          }
          break;
        case MSL_ACT_FX_SPECIAL_LW_LOOP: {
          if (stick_wants_turn(c, batch->state.input_main_x[idx], batch->state.facing[idx])) {
            enter_shine_ground_turn(batch, idx, ms);
            break;
          }
          const int16_t rl = (int16_t)ch->reflector_release_lag_frames;
          const int16_t af = batch->state.action_frame[idx];
          if ((held & (uint16_t)MSL_BUTTON_B) == 0) {
            if (rl == 0 || af >= (int16_t)(rl - 1)) {
              enter_shine_ground_end(batch, idx, ms);
            }
          }
        } break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP: {
          if (stick_wants_turn(c, batch->state.input_main_x[idx], batch->state.facing[idx])) {
            enter_shine_air_turn(batch, idx, ms);
            break;
          }
          const int16_t rl = (int16_t)ch->reflector_release_lag_frames;
          const int16_t af = batch->state.action_frame[idx];
          if ((held & (uint16_t)MSL_BUTTON_B) == 0) {
            if (rl == 0 || af >= (int16_t)(rl - 1)) {
              enter_shine_air_end(batch, idx, ms);
            }
          }
        } break;
        case MSL_ACT_FX_SPECIAL_LW_TURN: {
          const int16_t tf = (int16_t)ch->reflector_turn_frames;
          const int16_t af = batch->state.action_frame[idx];
          if (tf > 0 && af >= (int16_t)(tf - 1)) {
            enter_shine_ground_loop(batch, idx, ms);
          } else if ((held & (uint16_t)MSL_BUTTON_B) == 0) {
            enter_shine_ground_end(batch, idx, ms);
          }
        } break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_TURN: {
          const int16_t tf = (int16_t)ch->reflector_turn_frames;
          const int16_t af = batch->state.action_frame[idx];
          if (tf > 0 && af >= (int16_t)(tf - 1)) {
            enter_shine_air_loop(batch, idx, ms);
          } else if ((held & (uint16_t)MSL_BUTTON_B) == 0) {
            enter_shine_air_end(batch, idx, ms);
          }
        } break;
        case MSL_ACT_FX_SPECIAL_LW_END:
          if (anim_finished(cid, ms->speciallw_ground_end, anim_frame_f32)) {
            enter_wait(batch, idx);
          }
          break;
        case MSL_ACT_FX_SPECIAL_AIR_LW_END:
          if (anim_finished(cid, ms->speciallw_air_end, anim_frame_f32)) {
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

void shine_update_post_collision(MslBatch* batch) {
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
      const MslSpecialMsids* ms = msl_special_msids(cid);
      if (ms == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
      const float cur_frame = batch->state.anim_frame_f32[idx];

      if (action_is_shine_ground(a) && !on_ground) {
        // Ground -> air: preserve anim frame.
        batch->state.action_id[idx] = (uint16_t)(a + (uint16_t)MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA);
        // Remap msid per-state.
        const uint16_t a2 = batch->state.action_id[idx];
        if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_start;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_LOOP) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_HIT) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_hit;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_END) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_end;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_TURN) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_air_loop;
        }
        msl_anim_timebase_enter(batch, idx, cur_frame, 1.0f);
      } else if (action_is_shine_air(a) && on_ground) {
        // Air -> ground: preserve anim frame.
        batch->state.action_id[idx] = (uint16_t)(a - (uint16_t)MSL_FX_SHINE_GROUND_TO_AIR_ACTION_DELTA);
        const uint16_t a2 = batch->state.action_id[idx];
        if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_start;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_LOOP) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_HIT) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_hit;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_END) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_end;
        } else if (a2 == (uint16_t)MSL_ACT_FX_SPECIAL_LW_TURN) {
          batch->state.animation_index[idx] = (uint32_t)ms->speciallw_ground_loop;
        }
        msl_anim_timebase_enter(batch, idx, cur_frame, 1.0f);
      }
    }
  }
}
