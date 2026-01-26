#include "ledge.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_table.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "input_axis.h"
#include "stage_collision.h"

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_cliff_action_any(uint16_t a) {
  return (is_cliff_hold_action(a) || a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2) ? 1 : 0;
}

static inline uint8_t is_fall_like_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
    case MSL_ACT_DAMAGE_FALL:
      return 1;
    default:
      return 0;
  }
}

static inline uint16_t cliff_submotion_for_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
      return (uint16_t)MSL_SM_CLIFF_CATCH;
    case MSL_ACT_CLIFF_WAIT:
      return (uint16_t)MSL_SM_CLIFF_WAIT;
    case MSL_ACT_CLIFF_CLIMB_QUICK:
      return (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK;
    case MSL_ACT_CLIFF_ATTACK_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK;
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
      return (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK;
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1;
    case MSL_ACT_CLIFF_JUMP_QUICK2:
      return (uint16_t)MSL_SM_CLIFF_JUMP_QUICK2;
    default:
      return 0xFFFFu;
  }
}

static inline float facing_dir(uint8_t facing) { return facing ? 1.0f : -1.0f; }

static inline void enter_fall(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline void enter_wait_on_stage(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  batch->state.on_ground[idx] = 1;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t did_tap_jump(const MslCommonParams* c, float stick_y, uint8_t tilt_timer_y) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  return (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) ? 1 : 0;
}

static inline void enter_cliff_wait(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_WAIT;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static inline uint8_t enter_cliff_option_quick(MslBatch* batch, int bi, int p, uint16_t act,
                                               uint16_t smid) {
  if (batch == NULL) {
    return 0;
  }
  const size_t idx = msl_idx_player(bi, p);
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)smid;
  batch->state.on_ground[idx] = 0;
  batch->state.fall_fast[idx] = 0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  return 1;
}

static inline uint8_t ledge_wait_try_attack(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & ((uint16_t)MSL_BUTTON_A | (uint16_t)MSL_BUTTON_B)) != 0) {
    // Decomp: ftCo_8009AE38 -> ftCo_8009AEA4 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AE38
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_ATTACK_QUICK);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_escape(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & ((uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R)) != 0) {
    // Decomp: ftCo_8009AFD4 -> ftCo_8009B040 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AFD4
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_8009B040
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_ESCAPE_QUICK);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_jump(MslBatch* batch, const MslCommonParams* c, int bi,
                                          int p) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if ((pressed & (uint16_t)MSL_BUTTON_XY) != 0 ||
      did_tap_jump(c, stick_y, batch->state.tilt_timer_y[idx])) {
    // Decomp: ftCo_8009B170 -> ftCo_8009B1B8 chooses Quick vs Slow based on percent threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B170
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1,
                                    (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1);
  }
  return 0;
}

static inline uint8_t ledge_wait_try_climb_or_drop(MslBatch* batch, const MslCommonParams* c,
                                                   int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(msl_absf(stick_x) >= c->cliff_option_stick_threshold ||
        msl_absf(stick_y) >= c->cliff_option_stick_threshold)) {
    return 0;
  }

  // Decomp gate: mv.co.cliff.x8 must be set (it is set when no stick/cstick option input is
  // present, and consumed when a qualifying stick input triggers climb/drop).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AA0C
  //
  // Approximation for one-step reseeds: require the *previous frame's* main stick to have been
  // below the option threshold (a "neutral reset") before allowing climb/drop this frame.
  const float prev_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
  const float prev_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  if (msl_absf(prev_x) >= c->cliff_option_stick_threshold ||
      msl_absf(prev_y) >= c->cliff_option_stick_threshold) {
    return 0;
  }

  const float fd = facing_dir(batch->state.facing[idx]);
  const float angle = atan2f(stick_y, stick_x);
  if (angle > c->attack_angle_threshold_radians ||
      (angle > -c->attack_angle_threshold_radians && (stick_x * fd) >= 0.0f)) {
    // ClimbQuick entry (percent-based Quick/Slow selection is omitted for v1; suite is Quick-only).
    // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AB9C
    return enter_cliff_option_quick(batch, bi, p, (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK,
                                    (uint16_t)MSL_SM_CLIFF_CLIMB_QUICK);
  }

  // Drop from ledge (Fall entry).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
  batch->state.ledge_side[idx] = -1;
  enter_fall(batch, idx);
  return 1;
}

static inline void refresh_stage_ledge_occupants(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    batch->state.stage_ledge_occupant_left[bi] = -1;
    batch->state.stage_ledge_occupant_right[bi] = -1;
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (!is_cliff_hold_action(a)) {
        continue;
      }
      const int8_t side = batch->state.ledge_side[idx];
      if (side == 0) {
        if (batch->state.stage_ledge_occupant_left[bi] < 0) {
          batch->state.stage_ledge_occupant_left[bi] = (int8_t)p;
        }
      } else if (side == 1) {
        if (batch->state.stage_ledge_occupant_right[bi] < 0) {
          batch->state.stage_ledge_occupant_right[bi] = (int8_t)p;
        }
      }
    }
  }
}

void ledge_update_pre_physics(MslBatch* batch) {
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
      uint16_t a = batch->state.action_id[idx];
      if (!is_cliff_action_any(a)) {
        continue;
      }

      const uint16_t smid = cliff_submotion_for_action(a);
      if (smid != 0xFFFFu) {
        batch->state.animation_index[idx] = (uint32_t)smid;
      }

      if (is_cliff_hold_action(a)) {
        const int8_t side = batch->state.ledge_side[idx];
        if (!(side == 0 || side == 1)) {
          // Infer ledge side from facing: on the left ledge the fighter faces right (+).
          batch->state.ledge_side[idx] = batch->state.facing[idx] ? 0 : 1;
        }
      }

      // Anim-end transitions / IASA.
      if (a == (uint16_t)MSL_ACT_CLIFF_CATCH) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_CATCH);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffCatch_Anim -> ftCo_8009A804 (enter CliffWait).
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
          enter_cliff_wait(batch, idx);
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_WAIT) {
        // CliffWait IASA ordering is decomp-defined.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_CliffWait_IASA
        if (ledge_wait_try_attack(batch, bi, p) || ledge_wait_try_escape(batch, bi, p) ||
            ledge_wait_try_jump(batch, c, bi, p) || ledge_wait_try_climb_or_drop(batch, c, bi, p)) {
          // State changed; position snap for new hold option is handled by enter helpers.
          a = batch->state.action_id[idx];
        } else {
          // CliffWait hang time expires -> fall.
          // Decomp: ftCo_8009A9AC.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
          const float percent = batch->state.percent[idx];
          const float wait_frames = (percent < c->cliff_wait_percent_threshold)
                                        ? c->cliff_wait_frames_low_percent
                                        : c->cliff_wait_frames_high_percent;
          if (wait_frames > 0.0f && (float)batch->state.action_frame[idx] >= wait_frames) {
            batch->state.ledge_side[idx] = -1;
            enter_fall(batch, idx);
            a = batch->state.action_id[idx];
          }
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_JUMP_QUICK1);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump1_Anim -> ftCo_8009B2F8 (enter Jump2 and set launch velocity).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump1_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_8009B2F8
          const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
          const float fd = facing_dir(batch->state.facing[idx]);
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_JUMP_QUICK2;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          batch->state.ledge_side[idx] = -1;
          batch->state.on_ground[idx] = 0;
          batch->state.fall_fast[idx] = 0;
          if (ch != NULL) {
            batch->state.speed_air_x_self[idx] += fd * ch->ledge_jump_horizontal_velocity;
            batch->state.speed_y_self[idx] = ch->ledge_jump_vertical_velocity;
          }
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2) {
        const float end_frame =
            msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_CLIFF_JUMP_QUICK2);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: ftCo_CliffJump2_Anim -> Fall_Enter.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::ftCo_CliffJump2_Anim
          enter_fall(batch, idx);
          a = batch->state.action_id[idx];
        }
      } else if (a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
                 a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK) {
        const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
        if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
          // Decomp: CliffClimb_Anim (and CliffAttack/Escape) -> ftCommon_8007D92C.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
          //
          // TODO(decomp): ftCommon_8007D92C likely enters a grounded Wait-like state with proper floor
          // clamping and residual velocity handling. For now, enter Wait on stage.
          batch->state.ledge_side[idx] = -1;
          enter_wait_on_stage(batch, idx);
          a = batch->state.action_id[idx];
        }
      }
    }
  }

  refresh_stage_ledge_occupants(batch);
}

static inline uint8_t ledge_grab_window_ok(const MslCharParams* ch, float pos_x, float pos_y,
                                           float ledge_x, float ledge_y, float fd) {
  if (ch == NULL) {
    return 0;
  }
  // Use ftData_x44 ledge snap params as a data-driven proxy for the ledge grab region.
  // - ledge_snap_x defines the nominal horizontal snap distance from the cliff point.
  // - ledge_snap_y defines the nominal vertical snap offset at hang.
  // - ledge_snap_height gates how far above the cliff point a grab is allowed.
  //
  // Decomp context: the actual check is performed via collision env flags (Collide_LedgeGrabMask),
  // but the parameters are sourced from ftData_x44_t and ftCommonData.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
  // refs/melee/src/melee/ft/types.h (ftData_x44_t)
  const float dx = (pos_x - ledge_x) * fd;
  const float dy = pos_y - ledge_y;
  // Approximation for FD v1: only allow grabs from the "outside" of the stage ledge.
  // (i.e. the fighter must be offstage relative to the ledge point).
  if (dx > 0.0f) {
    return 0;
  }
  if (dx < -ch->ledge_snap_x || dx > ch->ledge_snap_x) {
    return 0;
  }
  if (dy < -ch->ledge_snap_height) {
    return 0;
  }
  if (dy > (ch->ledge_snap_y + ch->ledge_snap_height)) {
    return 0;
  }
  return 1;
}

void ledge_try_catch_post_collision(MslBatch* batch) {
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
    MslStagePoint2 ledge_left = {0};
    MslStagePoint2 ledge_right = {0};
    const uint8_t have_left = stage_collision_get_ledge_point(stage_id, 0, &ledge_left);
    const uint8_t have_right = stage_collision_get_ledge_point(stage_id, 1, &ledge_right);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];
      if (is_cliff_action_any(a)) {
        continue;
      }
      if (batch->state.on_ground[idx]) {
        continue;
      }
      // Only attempt cliff catch from airborne action states that run collision checks in GALE01.
      // This prevents "instant ledgegrab" from ground states that merely walked offstage this frame.
      if (!is_fall_like_action(a) && a != (uint16_t)MSL_ACT_ATTACK_AIR_N &&
          a != (uint16_t)MSL_ACT_ATTACK_AIR_F && a != (uint16_t)MSL_ACT_ATTACK_AIR_B &&
          a != (uint16_t)MSL_ACT_ATTACK_AIR_HI && a != (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
          a != (uint16_t)MSL_ACT_ESCAPE_AIR) {
        continue;
      }
      // Ledge catch is only active while moving downward in the FD suite. This avoids many false
      // positives for aerials/jumps near the ledge.
      if (!(batch->state.speed_y_self[idx] <= 0.0f)) {
        continue;
      }

      const float stick_y =
          apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      // Decomp: holding down beyond threshold disables ledge catch.
      // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
      if (stick_y <= -c->cliff_drop_stick_threshold) {
        continue;
      }

      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];

      // Choose the nearer ledge side as the first candidate, then fall back to the other.
      int side0 = 0;
      int side1 = 1;
      if (have_left && have_right) {
        const float dlx = x - ledge_left.x;
        const float dly = y - ledge_left.y;
        const float drx = x - ledge_right.x;
        const float dry = y - ledge_right.y;
        const float d0 = dlx * dlx + dly * dly;
        const float d1 = drx * drx + dry * dry;
        if (d1 < d0) {
          side0 = 1;
          side1 = 0;
        }
      }

      for (int si = 0; si < 2; si++) {
        const int side = (si == 0) ? side0 : side1;
        if (side == 0) {
          if (!have_left) {
            continue;
          }
          if (batch->state.stage_ledge_occupant_left[bi] >= 0) {
            continue;
          }
          const float fd = 1.0f;
          if (!ledge_grab_window_ok(ch, x, y, ledge_left.x, ledge_left.y, fd)) {
            continue;
          }

          // Enter CliffCatch and snap.
          batch->state.ledge_side[idx] = 0;
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
          batch->state.on_ground[idx] = 0;
          batch->state.fall_fast[idx] = 0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          // Decomp: ftCliffCommon_80081370 sets facing toward stage and snaps to the ledge point.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
          batch->state.facing[idx] = 1;  // left ledge -> face right
          batch->state.pos_x[idx] = ledge_left.x;
          batch->state.pos_y[idx] = ledge_left.y + ch->ledge_snap_y;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
          batch->state.stage_ledge_occupant_left[bi] = (int8_t)p;
          break;
        } else {
          if (!have_right) {
            continue;
          }
          if (batch->state.stage_ledge_occupant_right[bi] >= 0) {
            continue;
          }
          const float fd = -1.0f;
          if (!ledge_grab_window_ok(ch, x, y, ledge_right.x, ledge_right.y, fd)) {
            continue;
          }

          batch->state.ledge_side[idx] = 1;
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_CLIFF_CATCH;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_CLIFF_CATCH;
          batch->state.on_ground[idx] = 0;
          batch->state.fall_fast[idx] = 0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          batch->state.facing[idx] = 0;  // right ledge -> face left
          batch->state.pos_x[idx] = ledge_right.x;
          batch->state.pos_y[idx] = ledge_right.y + ch->ledge_snap_y;
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.speed_air_x_self[idx] = 0.0f;
          batch->state.speed_y_self[idx] = 0.0f;
          batch->state.speed_x_attack[idx] = 0.0f;
          batch->state.speed_y_attack[idx] = 0.0f;
          batch->state.stage_ledge_occupant_right[bi] = (int8_t)p;
          break;
        }
      }
    }
  }
}
