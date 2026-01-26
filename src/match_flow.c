#include "match_flow.h"

#include "action_ids.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "stage_collision.h"

enum { MSL_ANIM_NONE_U32 = 0xFFFFFFFFu };

static inline uint8_t match_flow_is_dead_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
    case MSL_ACT_DEAD_UP_STAR:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t match_flow_is_entry_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_ENTRY:
    case MSL_ACT_ENTRY_START:
    case MSL_ACT_ENTRY_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t match_flow_is_respawn_action(uint16_t a) {
  return (uint8_t)(a == MSL_ACT_REBIRTH || a == MSL_ACT_REBIRTH_WAIT);
}

static inline void freeze_no_submotion_timebase(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
  msl_anim_timebase_seed(batch, idx, -1.0f, 0.0f);
}

static inline void enter_fall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = 0;
  batch->state.tilt_timer_y[idx] = 0;
}

static inline float entry_x20(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0.0f;
  }
  const MslCharParams* phys = msl_char_params(batch->state.char_id[idx]);
  if (phys == NULL) {
    return 0.0f;
  }
  // Decomp: ft_0C31.c::ftCo_800C6408 computes:
  //   temp_f0 = fp->x34_scale.y * fp->co_attrs.trophy_scale;
  //   temp_f0_2 = 1.497345 * temp_f0;
  //   fp->mv.co.entry.x20 = temp_f0_2;
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6408
  const float trophy_scale = phys->trophy_scale;
  const float scale_y = batch->state.fighter_scale_y[idx];
  return (float)(1.497345f * trophy_scale * scale_y);
}

static inline float entry_base_y_from_entry_start(float pos_y, float x20, uint8_t timer,
                                                  int entry_start_frames) {
  if (entry_start_frames <= 0) {
    return pos_y;
  }
  const float denom = (float)entry_start_frames;
  const float t = (float)timer;
  const float frac = (denom - t) / denom;
  return pos_y - x20 * frac;
}

static inline float entry_base_y_from_entry_end(float pos_y, float x20, uint8_t timer,
                                                int entry_start_frames) {
  if (entry_start_frames <= 0) {
    return pos_y;
  }
  const float denom = (float)entry_start_frames;
  const float t = (float)timer;
  const float frac = t / denom;
  return pos_y - x20 * frac;
}

static inline void enter_entry_start(MslBatch* batch, size_t idx, const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const int n = (int)c->entry_start_frames;
  const uint8_t start_timer = (uint8_t)((n > 1) ? (n - 1) : 0);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ENTRY_START;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ENTRY_START;
  batch->state.match_flow_timer[idx] = start_timer;
  batch->state.on_ground[idx] = 0;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  // EntryStart uses a real submotion; start its timebase at 0.0.
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // Position update for the first EntryStart frame after transition:
  // - EntryStart_Anim decrements timer before Phys.
  // - EntryStart_Phys uses:
  //     frac = (x6BC - timer) / x6BC
  //     pos_y = base_y + x20 * frac
  // Here, `start_timer` is already (x6BC - 1), matching the post-frame for the first EntryStart
  // frame in the suite.
  const float x20 = entry_x20(batch, idx);
  if (n > 0) {
    const float denom = (float)n;
    const float frac = (denom - (float)start_timer) / denom;
    batch->state.pos_y[idx] = batch->state.pos_y[idx] + x20 * frac;
  }
}

static inline void enter_entry_end(MslBatch* batch, size_t idx, const MslCommonParams* c,
                                   float base_y, float x20) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const int n = (int)c->entry_end_frames;
  const uint8_t timer = (uint8_t)(n > 0 ? n : 0);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ENTRY_END;
  batch->state.match_flow_timer[idx] = timer;
  batch->state.on_ground[idx] = 0;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;

  // EntryEnd is observed with no submotion in the suite (animation_index=0xFFFFFFFF),
  // with action_frame=-1 and state_age=-1.
  freeze_no_submotion_timebase(batch, idx);

  // Decomp: ft_0C31.c::ftCo_800C6B6C sets:
  //   fp->cur_pos.y = fp->mv.co.entry.x4 + fp->mv.co.entry.x20;
  // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6B6C
  batch->state.pos_y[idx] = base_y + x20;
}

static inline void enter_rebirth(MslBatch* batch, size_t idx, const MslCommonParams* c,
                                 uint32_t stage_id, int port0) {
  if (batch == NULL || c == NULL) {
    return;
  }
  MslStageBounds cam = {0};
  MslStagePoint2 respawn = {0};
  if (!stage_collision_get_cam_bounds_world(stage_id, &cam) ||
      !stage_collision_get_respawn_point(stage_id, port0, &respawn)) {
    return;
  }

  // Decomp: respawn starts at camera top (world) and falls to the spawn platform.
  // refs/melee/src/melee/gr/stage.c::Stage_GetCamBoundsTopOffset
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBIRTH;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  batch->state.on_ground[idx] = 0;
  batch->state.pos_x[idx] = respawn.x;
  batch->state.pos_y[idx] = cam.top;

  // Rebirth fall speed is stage-derived: linear fall from camera top to platform Y over the
  // Rebirth timer length (p_ftCommonData + 0x5D0).
  // Decomp: refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (Rebirth enter sets fp->x2340 from 0x5D0)
  const int frames = (int)c->rebirth_timer_frames;
  float vy = 0.0f;
  if (frames > 0) {
    vy = (respawn.y - cam.top) / (float)frames;
  }
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = vy;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.match_flow_timer[idx] = (uint8_t)(frames > 0 ? (frames > 255 ? 255 : frames) : 0);
}

static inline void enter_rebirth_wait(MslBatch* batch, size_t idx, uint32_t stage_id, int port0) {
  if (batch == NULL) {
    return;
  }
  MslStagePoint2 respawn = {0};
  if (!stage_collision_get_respawn_point(stage_id, port0, &respawn)) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBIRTH_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.on_ground[idx] = 0;
  batch->state.pos_x[idx] = respawn.x;
  batch->state.pos_y[idx] = respawn.y;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  // RebirthWait timer is p_ftCommonData + 0x5D4; it can end early via IASA (inputs).
  // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_Anim / ftCo_RebirthWait_IASA
  const MslCommonParams* c = msl_common_params();
  const int frames = (c != NULL) ? (int)c->rebirth_wait_timer_frames : 0;
  batch->state.match_flow_timer[idx] = (uint8_t)(frames > 0 ? (frames > 255 ? 255 : frames) : 0);
}

void match_flow_update_pre_anim(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];

      if (!(match_flow_is_dead_action(a) || match_flow_is_respawn_action(a) ||
            match_flow_is_entry_action(a))) {
        continue;
      }

      // Freeze timebase for suite-observed "no submotion" match-flow states.
      // (Entry, EntryEnd, and most Dead* are observed with animation_index=0xFFFFFFFF.)
      if (a == (uint16_t)MSL_ACT_ENTRY || a == (uint16_t)MSL_ACT_ENTRY_END ||
          a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
          a == (uint16_t)MSL_ACT_DEAD_RIGHT) {
        freeze_no_submotion_timebase(batch, idx);
      }
      if (a == (uint16_t)MSL_ACT_ENTRY_END) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }
      if (a == (uint16_t)MSL_ACT_ENTRY) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }
      if (a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
          a == (uint16_t)MSL_ACT_DEAD_RIGHT) {
        batch->state.animation_index[idx] = MSL_ANIM_NONE_U32;
      }

      // Common invariants for match-flow states (suite-observed).
      batch->state.on_ground[idx] = 0;

      uint8_t prev_t = batch->state.match_flow_timer[idx];
      uint8_t t = prev_t;
      if (t > 0) {
        t--;
      }
      batch->state.match_flow_timer[idx] = t;

      // Entry position updates (timer-driven).
      if (a == (uint16_t)MSL_ACT_ENTRY_START || a == (uint16_t)MSL_ACT_ENTRY_END) {
        const int entry_start_frames = (int)c->entry_start_frames;
        const float x20 = entry_x20(batch, idx);
        if (a == (uint16_t)MSL_ACT_ENTRY_START) {
          // Update to the next-frame position using the decremented timer (t).
          const float base_y =
              entry_base_y_from_entry_start(batch->state.pos_y[idx], x20,
                                            (uint8_t)(t + 1), entry_start_frames);
          if (t == 0) {
            enter_entry_end(batch, idx, c, base_y, x20);
          } else if (entry_start_frames > 0) {
            const float denom = (float)entry_start_frames;
            const float frac = (denom - (float)t) / denom;
            batch->state.pos_y[idx] = base_y + x20 * frac;
          }
        } else {
          // EntryEnd keeps animation frozen; update position while the timer is active.
          const float base_y = entry_base_y_from_entry_end(batch->state.pos_y[idx], x20,
                                                          (uint8_t)(t + 1),
                                                          entry_start_frames);
          if (t == 0) {
            // Transition to Fall; preserve the current EntryEnd position so Fall physics can take
            // over from the last EntryEnd pose.
            enter_fall(batch, idx);
            batch->state.match_flow_timer[idx] = 0;
          } else if (entry_start_frames > 0) {
            const float denom = (float)entry_start_frames;
            const float frac = (float)t / denom;
            batch->state.pos_y[idx] = base_y + x20 * frac;
          }
        }
      } else if (a == (uint16_t)MSL_ACT_ENTRY) {
        // Entry -> EntryStart on timer expiry. Entry keeps the fighter invisible and stationary.
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        if (t == 0) {
          // Enter EntryStart; apply its first-frame position update immediately.
          enter_entry_start(batch, idx, c);
        }
      } else if (a == (uint16_t)MSL_ACT_DEAD_DOWN || a == (uint16_t)MSL_ACT_DEAD_LEFT ||
                 a == (uint16_t)MSL_ACT_DEAD_RIGHT || a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        if (a == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          // DeadUpStar stock loss is delayed until late in the animation.
          // Assembly shows the stock-loss event happens when the internal phase-1 timer expires,
          // leaving `dead_up_star_phase2_frames` frames remaining in the DeadUpStar motion.
          // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
          const int phase2 = (int)c->dead_up_star_phase2_frames;
          if (phase2 > 0 && (int)prev_t == phase2 + 1) {
            if (batch->state.stocks[idx] > 0) {
              batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
            }
          }
        }
        // Death -> Rebirth.
        if (a != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
        }
        if (t == 0) {
          enter_rebirth(batch, idx, c, stage_id, p);
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH) {
        // Rebirth -> RebirthWait.
        if (t == 0) {
          enter_rebirth_wait(batch, idx, stage_id, p);
        }
      } else if (a == (uint16_t)MSL_ACT_REBIRTH_WAIT) {
        // RebirthWait -> Fall.
        batch->state.speed_air_x_self[idx] = 0.0f;
        batch->state.speed_ground_x_self[idx] = 0.0f;
        batch->state.speed_y_self[idx] = 0.0f;
        batch->state.speed_x_attack[idx] = 0.0f;
        batch->state.speed_y_attack[idx] = 0.0f;
        if (t == 0) {
          enter_fall(batch, idx);
          batch->state.match_flow_timer[idx] = 0;
        }
      }
    }
  }
}

void match_flow_update_post_anim(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ENTRY_START) {
        continue;
      }
      // EntryStart animation is short; state remains active beyond the submotion end. Clamp the
      // timebase at end_frame and freeze rate to match Slippi state_age/action_frame behavior.
      const float end_frame =
          msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_ENTRY_START);
      if (end_frame > 0.0f && batch->state.anim_frame_f32[idx] >= end_frame) {
        msl_anim_timebase_seed(batch, idx, end_frame, 0.0f);
      }
    }
  }
}

void match_flow_update_post_input(MslBatch* batch) {
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
      if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_REBIRTH_WAIT) {
        continue;
      }

      // RebirthWait can exit early via IASA (inputs) long before its timer expires (x2340).
      // Suite note: RebirthWait runs only a few frames in many replays.
      //
      // Decomp pointers:
      // - Timer decrement -> Fall when x2340 reaches 0: ft_0D31.s::ftCo_RebirthWait_Anim
      // - Early exit decision logic: ft_0D31.s::ftCo_RebirthWait_IASA
      //
      // NOTE: This is an intentionally simplified approximation for suite coverage: any non-neutral
      // input exits RebirthWait to Fall. It is not decomp-exact.
      // TODO(decomp): implement the actual ftCo_RebirthWait_IASA decision chain.
      const uint16_t buttons = batch->state.input_buttons[idx];
      const uint8_t l = batch->state.input_l[idx];
      const uint8_t r = batch->state.input_r[idx];
      const float stick_x =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);

      const uint8_t have_input = (uint8_t)((buttons != 0) || (l != 0) || (r != 0) ||
                                           (stick_x != 0.0f) || (stick_y != 0.0f));
      if (!have_input) {
        continue;
      }

      enter_fall(batch, idx);
      batch->state.match_flow_timer[idx] = 0;
    }
  }
}

static inline uint8_t match_flow_should_check_blastzone(uint16_t a) {
  return (uint8_t)(!(match_flow_is_dead_action(a) || match_flow_is_respawn_action(a) ||
                     match_flow_is_entry_action(a)));
}

void match_flow_update_post_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    MslStageBounds blast = {0};
    if (!stage_collision_get_blast_bounds_world(stage_id, &blast)) {
      continue;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (!match_flow_should_check_blastzone(a)) {
        continue;
      }
      const uint8_t stocks = batch->state.stocks[idx];
      if (stocks == 0) {
        continue;
      }

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];

      uint16_t death = 0xFFFFu;
      if (x > blast.right) {
        death = (uint16_t)MSL_ACT_DEAD_RIGHT;
      } else if (x < blast.left) {
        death = (uint16_t)MSL_ACT_DEAD_LEFT;
      } else if (y > blast.top) {
        // Decomp: top KO requires being grounded, or a sufficient upward KB velocity.
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
        const uint8_t on_ground = batch->state.on_ground[idx] ? 1 : 0;
        if (on_ground || (batch->state.speed_y_attack[idx] > c->dead_up_kb_vel_threshold)) {
          death = (uint16_t)MSL_ACT_DEAD_UP_STAR;
        }
      } else if (y < blast.bottom) {
        death = (uint16_t)MSL_ACT_DEAD_DOWN;
      }

      if (death == 0xFFFFu) {
        continue;
      }

      // Enter Dead*:
      // - Decrement stock once (except DeadUpStar, which delays stock loss).
      // - Freeze motion/physics.
      // Decomp pointers:
      // - Blastzone check: refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
      // - Stock loss: refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D34E0
      if (death != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        if (batch->state.stocks[idx] > 0) {
          batch->state.stocks[idx] = (uint8_t)(batch->state.stocks[idx] - 1);
        }
      }

      batch->state.action_id[idx] = death;
      batch->state.on_ground[idx] = 0;
      batch->state.speed_air_x_self[idx] = 0.0f;
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.speed_y_self[idx] = 0.0f;
      batch->state.speed_x_attack[idx] = 0.0f;
      batch->state.speed_y_attack[idx] = 0.0f;
      batch->state.hitlag[idx] = 0;
      batch->state.hitstun[idx] = 0;

      if (death == (uint16_t)MSL_ACT_DEAD_UP_STAR) {
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_DAMAGE_FALL;
        // DeadUpStar is observed with action_frame starting at 0 and advancing; set timebase to 0.
        msl_anim_timebase_seed(batch, idx, 0.0f, 1.0f);
        // DeadUpStar enter initializes an internal timer from p_ftCommonData->x504, then runs two
        // phases (x508/x50C). Model this with a single countdown.
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D40B8
        const int phase1 = (int)c->dead_up_star_phase1_frames;
        const int phase2 = (int)c->dead_up_star_phase2_frames;
        int total = (int)c->dead_up_star_initial_frames;
        if (phase1 > 0) {
          total += phase1;
        }
        if (phase2 > 0) {
          total += phase2;
        }
        if (total < 0) {
          total = 0;
        }
        if (total > 255) {
          total = 255;
        }
        batch->state.match_flow_timer[idx] = (uint8_t)total;
      } else {
        // DeadDown/Left/Right are observed with no submotion and frozen state_age=-1.
        freeze_no_submotion_timebase(batch, idx);
        batch->state.match_flow_timer[idx] =
            (uint8_t)(c->dead_timer_frames > 255 ? 255 : c->dead_timer_frames);
      }
    }
  }
}
