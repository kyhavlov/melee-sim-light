#include "action.h"
#include "shields.h"
#include "falcon_specials.h"
#include "marth_specials.h"
#include "sheik_specials.h"

#include "ids.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "dash_iasa.h"
#include "escapeair_collision_owner.h"
#include "ftcommon_ecb.h"
#include "input_axis.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "trigger_input.h"
#include "jump_input.h"
#include "knockdown.h"
#include "blaster.h"
#include "shine.h"
#include "ledge.h"
#include "grab_flow.h"
#include "guard_lifecycle.h"
#include "throw_flow.h"
#include "stage_collision.h"

// -----------
// EscapeAir.c
// -----------

static inline void enter_fall_special(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  // Decomp: ftCo_EscapeAir_Anim -> ftCo_80096900(..., allow_interrupt=false, ...).
  // ftCo_80096900 routes through inline0, which enters FallSpecial with Ft_MF_KeepFastFall.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
  const uint8_t keep_fastfall = batch->state.fall_fast[idx] ? 1u : 0u;
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_SPECIAL;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_SPECIAL;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = keep_fastfall;
  // Decomp: EscapeAir enters FallSpecial via ftCo_80096900(..., arg1=1, ...), which sets xC=1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c and ftCo_FallSpecial.c
  batch->state.fallspecial_xc[idx] = 1;
  batch->state.fallspecial_landing_lag[idx] =
      (c != NULL) ? c->landing_fall_special_lag_frames : 0.0f;
  batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
}

static inline uint8_t action_floor_line_y_at_x(const MslBatch* batch, size_t idx, uint32_t stage_id,
                                               uint16_t floor_id, float x, float* y_out,
                                               uint8_t* x_within_out) {
  if (batch == NULL || y_out == NULL) {
    return 0u;
  }
  if (x_within_out != NULL) {
    *x_within_out = 0u;
  }
  const int line_idx = stage_collision_floor_line_index(stage_id, floor_id);
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  MslStageFloorLine line = {0};
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  if (!stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &line)) {
    return 0u;
  }
  if (x_within_out != NULL) {
    const float min_x = line.x0 < line.x1 ? line.x0 : line.x1;
    const float max_x = line.x0 > line.x1 ? line.x0 : line.x1;
    *x_within_out = (x >= min_x - 0.0001f && x <= max_x + 0.0001f) ? 1u : 0u;
  }
  const float dx = line.x1 - line.x0;
  if (fabsf(dx) <= 1e-6f) {
    *y_out = line.y0;
    return 1u;
  }
  const float t = (x - line.x0) / dx;
  *y_out = line.y0 + t * (line.y1 - line.y0);
  return 1u;
}

static inline uint8_t action_stage_has_soft_platform_floor(uint32_t stage_id) {
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL) {
    return 0u;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].is_platform ||
        stage_collision_floor_line_has_platform_transform(stage_id, g->lines[i].segment_i)) {
      return 1u;
    }
  }
  return 0u;
}

uint8_t escape_air_try_enter_from_air_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                 size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }

  // Decomp: ftCo_80099A58 uses `fp->input.x668 & (HSD_PAD_R|HSD_PAD_L)` (pressed-edge semantics).
  // This is the physical L/R edge, not the separate HSD_PAD_LR macro lane. Analog trigger and
  // Z synthesize HSD_PAD_LR for guard/timers/capture, but do not set physical HSD_PAD_L/R.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
  // refs/melee/src/melee/ft/fighter.c::{
  //   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  if ((buttons_pressed & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R)) == 0) {
    return 0;
  }

  // Enter EscapeAir and set initial self velocity.
  //
  // Decomp:
  // - If ABS(lstick.x) < escapeair_deadzone.x && ABS(lstick.y) < escapeair_deadzone.y: self_vel=(0,0)
  // - Else: angle = atan2f(lstick.y, lstick.x); self_vel = escapeair_force * (cosf, sinf)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C (inlineA0)
  // Note: fp->input.lstick is already global-deadzoned before EscapeAir checks the EscapeAir-specific
  // deadzone. Apply the same global deadzone here before the EscapeAir-specific deadzone.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C (fp->input.lstick)
  const float raw_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  const float raw_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  const float stick_x = apply_deadzone(raw_x, c->lstick_deadzone_x);
  const float stick_y = apply_deadzone(raw_y, c->lstick_deadzone_y);
  float vx = 0.0f;
  float vy = 0.0f;
  if (!(msl_absf(stick_x) < c->escapeair_deadzone_x &&
        msl_absf(stick_y) < c->escapeair_deadzone_y)) {
    // Decomp angle helper: ftCommon_8007D9D4 is atan2f(y, x).
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D9D4
    const float ang = atan2f(stick_y, stick_x);
    vx = c->escapeair_force * cosf(ang);
    vy = c->escapeair_force * sinf(ang);
  }

  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t source_action_id = batch->state.action_id[idx];
  const uint16_t floor_id = batch->state.ground_id[idx];
  MslStageFloorLineCaps source_floor_caps = {0};
  const uint8_t source_floor_has_caps =
      (floor_id != 0xFFFFu &&
       stage_collision_floor_line_caps(stage_id, floor_id, &source_floor_caps))
          ? 1u
          : 0u;
  const uint8_t source_floor_is_platform =
      (source_floor_has_caps && source_floor_caps.is_platform) ? 1u : 0u;
  const uint8_t source_floor_has_platform_transform =
      (source_floor_has_caps && source_floor_caps.platform_transform_kind != 0u) ? 1u : 0u;
  const uint8_t source_is_jumpaerial = (source_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                                        source_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                                           ? 1u
                                           : 0u;
  float source_floor_y = 0.0f;
  uint8_t source_floor_x_within = 0u;
  const uint8_t source_floor_y_valid =
      action_floor_line_y_at_x(batch, idx, stage_id, floor_id, batch->state.pos_x[idx],
                               &source_floor_y, &source_floor_x_within);
  const uint8_t source_floor_carries_locked_ecb =
      (floor_id != 0xFFFFu && (!source_floor_is_platform || source_floor_has_platform_transform))
          ? 1u
          : 0u;
  const uint8_t source_floor_is_offspan_ordinary_platform =
      (source_floor_is_platform && source_floor_has_platform_transform == 0u &&
       source_floor_x_within == 0u)
          ? 1u
          : 0u;
  const float escapeair_entry_next_root_y = batch->state.pos_y[idx] + vy;
  const uint8_t source_floor_is_offspan_transform =
      (source_floor_has_platform_transform && source_floor_y_valid != 0u &&
       source_floor_x_within == 0u)
          ? 1u
          : 0u;
  const uint8_t source_floor_is_offspan_sloped_ledge_main_floor =
      (source_floor_y_valid != 0u && source_floor_x_within == 0u &&
       source_floor_is_platform == 0u && source_floor_has_platform_transform == 0u &&
       stage_collision_floor_line_is_flat_between_sloped_ledges(stage_id, floor_id) &&
       batch->state.seed_prev_action_frame[idx] >= 3)
          ? 1u
          : 0u;
  const uint8_t stage_has_sloped_ledge_main_floor =
      stage_collision_stage_has_flat_between_sloped_ledges(stage_id);
  const uint8_t escapeair_entry_bottom_sweep_still_above_floor =
      (source_floor_is_offspan_transform || source_floor_is_offspan_sloped_ledge_main_floor ||
       (source_floor_y_valid &&
        (escapeair_entry_next_root_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
            (source_floor_y + 0.0001f)))
          ? 1u
          : 0u;
  if (batch->state.ecb_lock_timer[idx] != 0u &&
      batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
      batch->state.coll_desired_ecb_bottom_rel_y[idx] > 0.0001f && source_is_jumpaerial &&
      batch->state.action_frame[idx] >= 1 &&
      (escapeair_entry_bottom_sweep_still_above_floor ||
       (!source_floor_carries_locked_ecb && stage_has_sloped_ledge_main_floor))) {
    // Runtime EscapeAir entry can happen during JumpAerial IASA before Fighter_procMap.
    // Pass-through from a source floor-domain line still carries CollData_X130_Locked when source
    // `ftCo_EscapeAir_Coll` calls `mpColl_LoadECB_inline`, preserving the pre-entry
    // desired_ecb.bottom for the first EscapeAir callback only while the frame-start provenance is
    // still sustained JumpAerial. On generated sloped-ledge/main-floor shells, a stale visible
    // platform floor id can be off-domain while the following `EscapeAir_Coll` floor search is about
    // to cross a static source floor; the preserved desired bottom still belongs to CollData_X130
    // rather than to that stale visible floor id. On FoD, a height-transform platform floor is also
    // a source floor-domain line: the platform object owns the moving floor and the same CollData lock
    // handoff, unlike ordinary soft-platform candidates.
    // If the fighter has already moved off that transformed platform's horizontal span, do not use
    // the stale platform height to clear CollData ownership; the following EscapeAir_Coll floor
    // search owns the adjacent hard-floor handoff. Fresh cliff-jump chains, ordinary non-transform
    // platform air dodges, and zero-bottom air-dodge entries keep their ordinary EscapeAir floor
    // handoff; if the entered EscapeAir root is already deep enough that bottom.y crosses the floor
    // this frame, the normal floor publication path owns the immediate LandingFallSpecial
    // transition instead. Later EscapeAir_Coll callbacks still must consume a real
    // desired-bottom crossing before publishing LandingFallSpecial.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    //   ftCo_80099A58,ftCo_EscapeAir_Coll}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
    batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
        msl_escapeair_locked_bottom_owner_for_live_jumpaerial_entry(
            source_floor_is_offspan_ordinary_platform
                ? 0u
                : action_stage_has_soft_platform_floor(stage_id));
  } else if (source_is_jumpaerial) {
    // Other JumpAerial -> EscapeAir entries use the freshly loaded EscapeAir floor handoff. Clear
    // the runtime JumpAerial desired-bottom owner so platform-origin and zero-bottom air-dodges do
    // not inherit the narrower soft-platform pass-through path above. Already-seeded EscapeAir rows
    // bypass this entry callback and keep their explicit one-step seed lane.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
        (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ESCAPE_AIR;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ESCAPE_AIR;
  // Decomp: EscapeAir entry calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.speed_air_x_self[idx] = vx;
  batch->state.speed_y_self[idx] = vy;
  // Decomp: EscapeAir enters without KeepFastFall; treat EscapeAir as a self-velocity-controlled
  // state and clear any prior fall-fast latch.
  batch->state.fall_fast[idx] = 0;
  return 1;
}

void escape_air_update(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return;
  }

  // Anim end -> FallSpecial.
  // Decomp: ftCo_EscapeAir_Anim checks ftAnim_IsFramesRemaining.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_ESCAPE_AIR);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    enter_fall_special(batch, c, idx);
  }
}

// ---------
// Escape.c
// ---------

static inline void escape_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.fall_fast[idx] = 0u;
}

static inline void rebound_wait_restore_ground_from_carried_floor(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.on_ground[idx] != 0u) {
    return;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  if (ground_id == 0xFFFFu || stage_collision_floor_line_index(stage_id, ground_id) < 0) {
    return;
  }

  // Rebound_Anim exits through ft_8008A2BC -> ft_8008A348. If the source fighter is GA_Air,
  // ft_8008A348 calls ftCommon_8007D7FC before Fighter_ChangeMotionState(Wait).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A348
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  msl_ftcommon_8007d6a4(batch, ch, idx);
}

static inline void enter_escape_n(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80099894 -> ftCo_800998EC (non-Yoshi path).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:248-269.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_ESCAPE_N;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_ESCAPE_N;
  // Decomp: ftCo_800998EC calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_800998EC
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline void enter_escape_roll(MslBatch* batch, size_t idx, uint16_t action_id) {
  // Decomp: ftCo_8009917C -> ftCo_800992A8 -> ftCo_80099314 (default fighters).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:58-88 and :104-120.
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (action_id == (uint16_t)MSL_ACT_ESCAPE_F)
                                          ? (uint32_t)MSL_SM_ESCAPE_F
                                          : (uint32_t)MSL_SM_ESCAPE_B;
  // Decomp: ftCo_80099314 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099314
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
}

static inline uint8_t ucf_shielddrop_suppresses_spotdodge(const MslBatch* batch,
                                                          const MslCommonParams* c, size_t idx,
                                                          float stick_x, float stick_y,
                                                          float cstick_y, uint8_t tilt_timer_x) {
  if (batch == NULL || c == NULL || !batch->config.ucf_enabled) {
    return 0u;
  }
  if (cstick_y <= c->spotdodge_stick_y_threshold ||
      tilt_timer_x < c->escape_flick_tilt_max_frames || stick_y <= -0.8f) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    return 0u;
  }

  // UCF 0.84 Axe-method shield-drop spotdodge suppressor:
  // - The patch hooks the shared EscapeN entry and skips to the caller's false return when the
  //   current input is a rim-coordinate shield-drop attempt on a platform.
  // - C-stick down keeps spotdodge priority.
  // - Roll must be disabled (`stick_x_hold_time >= roll_stick_frames`).
  // - The Y gate is the patch-local `-.8000` threshold; stronger down still spotdodges.
  // refs/ucf/src/shielddrop/shielddrop.S
  // refs/ucf/src/pad_buffer/pad_buffer.cpp::is_rim_coord
  const float bias = 0.0001f;
  const int ix = (int)(msl_absf(stick_x) * 80.0f - bias) + 2;
  const int iy = (int)(msl_absf(stick_y) * 80.0f - bias) + 2;
  return (uint8_t)((ix * ix + iy * iy) > (80 * 80));
}

static inline uint8_t escape_try_enter_spotdodge_from_guard_y(MslBatch* batch,
                                                              const MslCommonParams* c, size_t idx,
                                                              float stick_x, float stick_y,
                                                              float cstick_y, uint8_t tilt_timer_x,
                                                              uint8_t tilt_timer_y) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Spotdodge (EscapeN) gate (Guard IASA path).
  // Decomp: ftCo_8009980C (stick.y + x671_timer_lstick_tilt_y) and cstick.y override path
  // (ftCo_800DF8E8), called from ftCo_GuardOn_IASA / ftCo_Guard_IASA / ftCo_GuardOff_IASA.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF8E8
  const uint8_t want_spotdodge = ((stick_y <= c->spotdodge_stick_y_threshold &&
                                   tilt_timer_y < c->spotdodge_flick_tilt_max_frames) ||
                                  (cstick_y <= c->spotdodge_stick_y_threshold))
                                     ? 1
                                     : 0;
  if (!want_spotdodge) {
    return 0;
  }
  if (ucf_shielddrop_suppresses_spotdodge(batch, c, idx, stick_x, stick_y, cstick_y,
                                          tilt_timer_x)) {
    return 0;
  }
  enter_escape_n(batch, idx);
  return 1;
}

static inline uint8_t escape_guard_wants_spotdodge(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  if (!((stick_y <= c->spotdodge_stick_y_threshold &&
         tilt_timer_y < c->spotdodge_flick_tilt_max_frames) ||
        (cstick_y <= c->spotdodge_stick_y_threshold))) {
    return 0u;
  }
  return ucf_shielddrop_suppresses_spotdodge(batch, c, idx, stick_x, stick_y, cstick_y,
                                             tilt_timer_x)
             ? 0u
             : 1u;
}

static inline uint8_t guard_entry_via_wait_callback_from_current_row(const MslBatch* batch,
                                                                     size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t a0 = batch->state.action_id[idx];
  const uint16_t prev = batch->state.prev_action_id[idx];
  // GuardOn can be entered after another state anim callback first promotes into Wait on the same
  // frame, then Wait_IASA consumes held shield. The next replay-visible GuardOn row is a frozen
  // snapshot, so preserve just the fact that entry came through that callback bridge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackS4.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
  if (a0 == (uint16_t)MSL_ACT_WAIT && prev != (uint16_t)MSL_ACT_WAIT) {
    return 1u;
  }
  return 0u;
}

static inline float guard_x650_from_input(const MslCommonParams* c, uint16_t buttons, uint8_t l,
                                          uint8_t r) {
  // Fighter input synthesis writes the source trigger lane in this order:
  // - analog max(L, R), deadzoned by p_ftCommonData->x10;
  // - digital L/R forces held_inputs|=HSD_PAD_LR and x650=1.0f;
  // - held Z then forces held_inputs|=HSD_PAD_LR|HSD_PAD_A and x650=p_ftCommonData->x14.
  // GuardOn entry (`ftCo_800921DC`) and hold drain (`ftCo_800925A4`) consume that x650 value,
  // which differs from the boolean "shield is held" predicate.
  // refs/melee/src/melee/ft/fighter.c:1868-1892
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4}
  if ((buttons & (uint16_t)MSL_BUTTON_Z) != 0u && c != NULL) {
    return c->z_button_trigger_value;
  }
  return msl_trigger_unit_from_input(buttons, l, r);
}

static inline uint8_t escape_try_enter_spotdodge_from_guard(MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  return escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                                 tilt_timer_x, tilt_timer_y);
}

static inline uint8_t wait_iasa_try_enter_spotdodge_before_guard_impl(MslBatch* batch,
                                                                      const MslCommonParams* c,
                                                                      size_t idx,
                                                                      uint8_t use_hsd_lr_lane) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }

  // Wait_IASA checks ftCo_80099794 before ftCo_80091A4C guard entry. ftCo_80099794 is narrower
  // than Guard IASA's ftCo_8009980C: it requires held L/R plus the inlineB0 down-stick gate and
  // does not consume the c-stick spotdodge helper.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{
  //   ftCo_80099794,ftCo_80099894,ftCo_800998EC}
  const uint16_t lr = use_hsd_lr_lane ? (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)
                                      : (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R);
  const uint16_t buttons = batch->state.input_buttons[idx];
  if ((buttons & lr) == 0u) {
    if (!use_hsd_lr_lane) {
      return 0u;
    }
    // Fighter input synthesis maps analog trigger values past p_ftCommonData->x10 into
    // held_inputs & HSD_PAD_LR before callbacks consume ftCo_80099794.
    // refs/melee/src/melee/ft/fighter.c:1868-1890
    const float trig =
        msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
    if (trig <= c->trigger_deadzone) {
      return 0u;
    }
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (!(stick_y <= c->spotdodge_stick_y_threshold &&
        batch->state.tilt_timer_y[idx] < c->spotdodge_flick_tilt_max_frames)) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  return escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                                 batch->state.tilt_timer_x[idx],
                                                 batch->state.tilt_timer_y[idx]);
}

uint8_t wait_iasa_try_enter_spotdodge_before_guard(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  return wait_iasa_try_enter_spotdodge_before_guard_impl(batch, c, idx, 0u);
}

uint8_t wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(MslBatch* batch, const MslCommonParams* c,
                                                          size_t idx) {
  return wait_iasa_try_enter_spotdodge_before_guard_impl(batch, c, idx, 1u);
}

uint8_t escape_try_enter_from_guard(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);

  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  // Spotdodge (EscapeN) has priority over rolls in Guard IASA.
  if (escape_try_enter_spotdodge_from_guard_y(batch, c, idx, stick_x, stick_y, cstick_y,
                                              tilt_timer_x, tilt_timer_y)) {
    return 1;
  }

  // Roll (EscapeF/EscapeB) chooses direction based on the triggering axis.
  // Decomp: ftCo_8009917C picks lstick.x if gated, else cstick.x (ftCo_800DF8B0), then chooses
  // EscapeF vs EscapeB based on stick_x * facing_dir.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:58-87 and refs/melee/src/melee/ft/ft_0DF1.c:224-244.
  float choose_x = 0.0f;
  uint8_t have_x = 0;
  if (msl_absf(stick_x) >= c->escape_stick_x_threshold &&
      tilt_timer_x < c->escape_flick_tilt_max_frames) {
    choose_x = stick_x;
    have_x = 1;
  } else if (msl_absf(cstick_x) >= c->escape_stick_x_threshold) {
    choose_x = cstick_x;
    have_x = 1;
  }
  if (have_x) {
    const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
    const uint16_t roll_act =
        (choose_x * facing_dir) >= 0.0f ? (uint16_t)MSL_ACT_ESCAPE_F : (uint16_t)MSL_ACT_ESCAPE_B;
    enter_escape_roll(batch, idx, roll_act);
    return 1;
  }

  return 0;
}

void escape_update_grounded(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                            size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL) {
    return;
  }

  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_ESCAPE_N && a != (uint16_t)MSL_ACT_ESCAPE_F &&
      a != (uint16_t)MSL_ACT_ESCAPE_B) {
    return;
  }

  uint32_t smid = 0xFFFFFFFFu;
  if (a == (uint16_t)MSL_ACT_ESCAPE_N) {
    smid = (uint32_t)MSL_SM_ESCAPE_N;
  } else if (a == (uint16_t)MSL_ACT_ESCAPE_F) {
    smid = (uint32_t)MSL_SM_ESCAPE_F;
  } else {
    smid = (uint32_t)MSL_SM_ESCAPE_B;
  }
  batch->state.animation_index[idx] = smid;

  if (a == (uint16_t)MSL_ACT_ESCAPE_F &&
      batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_F &&
      move_tables_escapef_should_flip_facing(batch->state.char_id[idx],
                                             batch->state.prev_action_frame[idx],
                                             batch->state.action_frame[idx])) {
    // Decomp: Escape_Anim flips facing when ftCheckThrowB3 consumes the script-owned bit.
    // The EscapeF script emits set_throw_flags(hit_idx=0) at the extracted action-frame threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim
    // refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
    // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
    // data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"]["events"] set_throw_flags
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
  }

  // NOTE: Escape ground velocity updates are a single-writer in physics_integrate().
  // Decomp:
  // - EscapeF/B phys: ftCo_Escape_Phys -> ft_80085004 -> ft_80085030
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Phys
  //   refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
  // - EscapeN phys: ftCo_EscapeN_Phys -> ft_80084F3C
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Phys
  //   refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C

  // Anim end -> Wait.
  // Decomp: ftCo_Escape_Anim / ftCo_EscapeN_Anim.
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)smid);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    if (a == (uint16_t)MSL_ACT_ESCAPE_F || a == (uint16_t)MSL_ACT_ESCAPE_B) {
      // Decomp: ftCo_Escape_Anim zeros gr_vel at end before entering Wait.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c:154-162.
      batch->state.speed_ground_x_self[idx] = 0.0f;
    }
    escape_enter_wait(batch, idx);
  }
}

// --------
// Guard.c
// --------

static inline void enter_guard_reflect_common_setup(MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_REFLECT;
  // Slippi post-frame `animation_index` is frequently -1 for shield states in our validation rows.
  // Keep this consistent with replay seeds/refs so validation compares cleanly.
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  batch->state.guard_reflect_timer_x14[idx] = msl_guard_reflect_timer_x14_init(c);
  batch->state.guard_reflect_timer_x18[idx] = msl_guard_reflect_timer_x18_init(c);
  batch->state.guard_reflect_entered_this_frame[idx] = 1u;
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = msl_guard_x10_visible_guardon_init_u8(c);
  batch->state.lightshield_amount[idx] = 0.0f;
  // GuardReflect entry publishes the timer-owned x221C lanes immediately:
  // ftCo_8009388C / ftCo_80093A50 set x221C_b3, x221C_b1, and x221C_b2 when the
  // GuardReflect motion state is installed. A later same-frame Fighter_ChangeMotionState
  // may clear b3, but b1/b2 remain timer-owned until ftCo_80093BC0 expires x14/x18.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |=
      (uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
}

static inline void enter_guard_reflect_from_guard(MslBatch* batch, const MslCommonParams* c,
                                                  size_t idx) {
  // Decomp entry path while already guarding:
  // - ftCo_80093694 -> ftCo_80093850 -> ftCo_8009388C.
  // - This already-shielding path is the GuardOn-origin provenance used by the final-x14
  //   ShieldDesc handoff.
  // - ftCo_8009388C keeps the current anim frame and does not call ftAnim_8006EBA4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  const float anim_start = batch->state.anim_frame_f32[idx];
  enter_guard_reflect_common_setup(batch, c, idx);
  batch->state.guard_reflect_origin_guardon[idx] = 1u;
  msl_anim_timebase_enter(batch, idx, anim_start, 1.0f);
  // ftCo_8009388C keeps the current anim frame on Guard->GuardReflect entry. Under teacher-forced
  // no-submotion snapshots, preserve negative carry-through when present; otherwise fall back to
  // frozen -1 shape used by Slippi snapshots.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009388C
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // Decomp ordering for GuardOn/GuardReflect path:
  // - GuardOn_Anim runs before GuardOn_IASA (same Fighter proc), then ftCo_80093694 can enter
  //   GuardReflect while keeping current anim frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
  //
  // For no-submotion snapshots (`animation_index==-1`, negative state_age/action_frame), preserve
  // that "Anim-before-IASA" consumption by stepping one frame deeper into the negative lane so
  // the next frame's action_frame matches Slippi's -1-lane snapshot shape.
  // Snapshot-parity only: this is not claiming GALE01 uses a persistent "-2" lane in normal play.
  msl_anim_timebase_seed(batch, idx, (anim_start < 0.0f) ? (anim_start - 1.0f) : -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline void enter_guard_reflect_from_locomotion(MslBatch* batch, const MslCommonParams* c,
                                                       size_t idx) {
  // Decomp entry path from locomotion guard check:
  // - ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50.
  // - ftCo_80093A50 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  enter_guard_reflect_common_setup(batch, c, idx);
  batch->state.guard_reflect_origin_guardon[idx] = 0u;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  // Slippi no-submotion shield snapshots are commonly encoded with animation_index=-1 and
  // state_age/action_frame=-1. Keep GuardReflect entry on that frozen timebase shape.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline uint8_t dash_iasa_guard_admission_reaches_terminal_scalar(
    const MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t action_id_start,
    float action_anim_frame_start) {
  if (batch == NULL || c == NULL || action_id_start != (uint16_t)MSL_ACT_DASH) {
    return 0u;
  }
  // Dash IASA's early x4 branch checks SpecialS/item/catchdash/AttackS4/EscapeF and then jumps to
  // block_42; guard admission helpers are only called from the mid/late branches. Fighter
  // callbacks see `cur_anim_frame` after the Anim callback has advanced the timebase for the
  // current frame, so compare the callback-time frame rather than the seed post-frame value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  const float frame_step = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
  const float callback_anim_frame = action_anim_frame_start + frame_step;
  if (batch->state.dash_x4[idx] != 0u && callback_anim_frame <= c->dash_iasa_x44) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t dash_iasa_try_enter_opposite_checkinput_turn_before_guard(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t action_id_start) {
  if (batch == NULL || c == NULL || action_id_start != (uint16_t)MSL_ACT_DASH) {
    return 0u;
  }

  const float cur_anim_frame = batch->state.anim_frame_f32[idx];
  if (batch->state.dash_x4[idx] != 0u && cur_anim_frame <= c->dash_iasa_x44) {
    return 0u;
  }

  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  if ((stick_x * facing_dir) >= 0.0f) {
    return 0u;
  }

  // Decomp: the mid Dash_IASA branch calls ftCo_Dash_CheckInput before ftCo_80091AD8, and
  // the late branch calls ftCo_Dash_CheckInput before ftCo_80091A4C. The opposite-facing
  // x3C/x40 path enters Turn and consumes the callback before GuardOn/GuardReflect can start.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
  //   ftCo_Dash_IASA,ftCo_Dash_CheckInput
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_80091A4C}
  if (msl_absf(stick_x) < c->dash_flick_abs ||
      batch->state.tilt_timer_x[idx] >= c->dash_flick_tilt_max_frames) {
    return 0u;
  }

  batch->state.turn_has_turned[idx] = 0;
  batch->state.turn_frames_to_turn[idx] = 0;
  batch->state.turn_x8[idx] = (int8_t)(facing_dir > 0.0f ? 1 : -1);
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_TURN;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_TURN;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  dash_iasa_apply_root_motion_exit_gr_vel_clamp(
      batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
  dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
  return 1u;
}

static inline uint8_t dash_iasa_try_enter_a_tap_jump_after_attack_s4_miss(MslBatch* batch,
                                                                          const MslCommonParams* c,
                                                                          size_t idx) {
  if (batch == NULL || c == NULL || batch->state.jumps_left[idx] == 0u) {
    return 0u;
  }
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_A) == 0u) {
    return 0u;
  }
  const float stick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  if (msl_absf(stick_x) >= c->dash_flick_abs &&
      batch->state.tilt_timer_x[idx] < c->dash_flick_tilt_max_frames) {
    return 0u;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (stick_y < c->tap_jump_threshold ||
      batch->state.tilt_timer_y[idx] >= c->tap_jump_tilt_max_frames) {
    return 0u;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.kneebend_jump_input[idx] = (uint8_t)MSL_JUMP_INPUT_LSTICK;
  batch->state.kneebend_is_short_hop[idx] = 0u;
  return 1u;
}

static inline uint8_t guard_on_entry_source_can_feed_followup_reflect(uint16_t action_id) {
  return action_id == (uint16_t)MSL_ACT_LANDING ? 1u : 0u;
}

static inline void enter_guard_on(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                  uint8_t entered_via_wait_callback) {
  // Decomp entry: ftCo_80091A4C -> ftCo_800923B4 -> ftCo_800924C0.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:66-69 and :313-327.
  const uint16_t source_action = batch->state.action_id[idx];
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_ON;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  // Decomp: ftCo_800924C0 calls ftAnim_8006EBA4 immediately after ChangeMotionState.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  // Slippi no-submotion shield snapshots are commonly encoded with animation_index=-1 and
  // state_age/action_frame=-1. Keep GuardOn entry on that frozen timebase shape.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
  // GuardOn entry clears fp+0x221C GuardReflect bits before entering shield hold:
  // - x221C_b3 = 0
  // - x221C_b1 = 0
  // - x221C_b2 = 0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield mapping)
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(
      uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
  batch->state.guard_on_entered_this_frame[idx] = 1u;
  // Keep the entry-family marker through the entry callback row and its immediate frozen
  // GuardOn_IASA handoff, then consume it below. The two ticks are runtime-only hidden source
  // state from the same proc window, not a replay seed lane or a persistent GuardOn property.
  batch->state.guard_entry_via_wait_callback[idx] = entered_via_wait_callback ? 2u : 0u;
  // Source-entry latch for the immediate GuardOn -> GuardReflect item ReflectDesc owner.
  // Landing's callback can enter GuardOn one frame before GuardOn_IASA consumes the LR edge into
  // ftCo_8009388C; run-family GuardOn controls remain on their ordinary item owner unless the
  // older seed/action lanes already prove the source GuardOn owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::*_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_GuardOn_IASA,ftCo_8009388C}
  batch->state.guard_on_entry_reflect_source_latch[idx] =
      guard_on_entry_source_can_feed_followup_reflect(source_action) ? 2u : 0u;
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = msl_guard_x10_visible_guardon_init_u8(c);
  {
    // GuardOn entry lightshield owner:
    // ftCo_800924C0 calls ftCo_800921DC before returning to the current callback. That source
    // helper initializes `fp->lightshield_amount` from the current `input.x650` trigger lane, so
    // every GuardOn entry path must publish the same held-trigger latch before the first GuardOn
    // drain. Keep this in the entry helper instead of only the generic grounded IASA call site:
    // Damage/Wait-style handoffs can enter GuardOn through the same source helper.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_800921DC}
    const float denom = 1.0f - c->trigger_deadzone;
    if (denom > 0.0f) {
      const float trig = guard_x650_from_input(
          c, batch->state.input_buttons[idx], batch->state.input_l[idx], batch->state.input_r[idx]);
      float light = (trig - c->trigger_deadzone) / denom;
      if (light < 0.0f) {
        light = 0.0f;
      } else if (light > 1.0f) {
        light = 1.0f;
      }
      batch->state.lightshield_amount[idx] = light;
    } else {
      batch->state.lightshield_amount[idx] = 0.0f;
    }
  }
}

uint8_t wait_iasa_try_guard_after_callback(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL || batch->state.shield_hp[idx] <= 0.0f) {
    return 0u;
  }
  // Decomp callback bridge:
  // - Several Anim callbacks enter grounded Wait via ft_8008A2BC / ft_8008A348.
  // - The destination Wait_IASA can then run in the same Fighter proc. Preserve the command order
  //   up to guard: pre-guard attack/special/catch commands block this helper, spotdodge comes
  //   before ftCo_80091A4C, and guard/powershield consumes only the source HSD_PAD_LR lane.
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint16_t pre_guard_buttons = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_Z);
  if ((pressed & pre_guard_buttons) != 0u) {
    return 0u;
  }
  const float cstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  if (cstick_x != 0.0f || cstick_y != 0.0f) {
    return 0u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
    return 1u;
  }

  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((pressed & (uint16_t)LR) != 0u &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_locomotion(batch, c, idx);
    return 1u;
  }

  const float trig =
      msl_trigger_unit_from_input(buttons, batch->state.input_l[idx], batch->state.input_r[idx]);
  const uint8_t shield_held_inputs =
      (((buttons & (uint16_t)LR) != 0u) || trig > c->trigger_deadzone) ? 1u : 0u;
  if (shield_held_inputs) {
    enter_guard_on(batch, c, idx, 1u);
    return 1u;
  }
  return 0u;
}

static inline void enter_guard_hold(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_800928CC -> ftCo_80092908 changes motion to ftCo_MS_Guard.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:421-446.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD;
  batch->state.animation_index[idx] = 0xFFFFFFFFu;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

  // Slippi parity for no-submotion shield snapshots:
  // `MSL_ACT_GUARD` is packed with animation_index=-1 and state_age/action_frame=-1 in the replay
  // suite (including GuardSetOff->Guard transitions), so keep Guard on the frozen (-1) timebase.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  msl_anim_timebase_seed(batch, idx, -1.0f,
                         msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]));
}

static inline void enter_guard_off(MslBatch* batch, size_t idx) {
  // Decomp: ftCo_80092BCC sets a release latch; Guard IASA transitions to GuardOff via ftCo_80092C54.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:481-509.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_GUARD_OFF;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = 0;
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void enter_shield_break_fly(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  // Shield depletion during GuardOn/Guard/GuardReflect Anim calls ftCo_800925A4, clears shield
  // active flags, enters ShieldBreakFly through ftCo_80098B20, and immediately ticks the animation.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_80098B20
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_SHIELD_BREAK_FLY;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_SHIELD_BREAK_FLY;
  msl_anim_timebase_enter_with_policy(batch, idx, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_IMMEDIATE);
  batch->state.on_ground[idx] = 0u;
  // ftCommon_8007D5D4 flips `ground_or_air` and locks ECB, but it does not clear the previous
  // floor line id; Slippi still exposes the Guard floor id on the break-entry post-frame.
  // ftCo_80098B20 then calls ftColl_8007B62C(..., 2), making the break state intangible through
  // the replay-visible merged hit-status lane.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C
  batch->state.jumps_left[idx] =
      (ch != NULL && ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  batch->state.hurtbox_state[idx] = 2u;
  msl_ftcommon_lock_ecb_8007d5d4(batch, idx);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  batch->state.speed_y_self[idx] = (ch != NULL) ? ch->shield_break_initial_velocity : 0.0f;
  batch->state.guard_release_latched_xc[idx] = 0;
  batch->state.guard_x10[idx] = 0;
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
  batch->state.lightshield_amount[idx] = 0.0f;
}

static inline void guard_enter_wait(MslBatch* batch, size_t idx) {
  // Decomp: ft_8008A2BC -> ft_8008A348 enters Wait with anim frame 0.0.
  // refs/melee/src/melee/ft/ft_0892.c:193-236.
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.guard_special_enable_timer_x1c[idx] = 0u;
}

static inline void shieldbreak_enter_stand(MslBatch* batch, size_t idx, uint16_t source_action) {
  if (batch == NULL) {
    return;
  }
  // ShieldBreakDown_Anim enters ShieldBreakStandU/D when its animation ends; the destination side
  // follows the source down motion, and the transition keeps collision-animation hit status.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_ShieldBreakDown_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
  const uint8_t up = (source_action == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U) ? 1u : 0u;
  batch->state.action_id[idx] =
      up ? (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_U : (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_D;
  batch->state.animation_index[idx] =
      up ? (uint32_t)MSL_SM_SHIELD_BREAK_STAND_U : (uint32_t)MSL_SM_SHIELD_BREAK_STAND_D;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.hurtbox_state[idx] = 2u;
}

static inline float furafura_timer_init(const MslBatch* batch, const MslCommonParams* c,
                                        size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0.0f;
  }
  // Decomp: ftCo_80099010 initializes the shared fp->grab_timer from percent.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  float percent_term = c->furafura_timer_percent_base - batch->state.percent[idx];
  if (percent_term < 0.0f) {
    percent_term = 0.0f;
  }
  return percent_term + c->furafura_timer_base;
}

static inline void shieldbreak_enter_furafura(MslBatch* batch, const MslCommonParams* c,
                                              size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // ShieldBreakStand_Anim enters Furafura through ftCo_80099010; this resets shield health and
  // initializes the common grab_timer lane used by Furafura_Anim.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_ShieldBreakStand_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_FURAFURA;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_FURAFURA;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.shield_hp[idx] = c->shield_break_reset_health;
  batch->state.capture_grab_timer[idx] = furafura_timer_init(batch, c, idx);
  // Furafura entry does not keep ShieldBreakStand's collision-animation hit status:
  // ftCo_80099010 changes motion with only SkipModel | SkipMatAnim, while ShieldBreakStand used
  // KeepColAnimHitStatus | SkipColAnim. Clear the hidden x198C timer/status lanes along with the
  // replay-visible hurtbox state on the destination row.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
  batch->state.colanim_hit_status_x198c[idx] = 0u;
  batch->state.colanim_timer_x1990[idx] = 0u;
  batch->state.colanim_timer_x1994[idx] = 0u;
  batch->state.hurtbox_state[idx] = 0u;
}

static inline uint8_t furafura_grab_mash_active(MslBatch* batch, const MslCommonParams* c,
                                                size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  float stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
  float stick_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  stick_x = apply_deadzone(stick_x, c->lstick_deadzone_x);
  stick_y = apply_deadzone(stick_y, c->lstick_deadzone_y);

  int8_t next_x = batch->state.grab_mash_stick_x_sign[idx];
  int8_t next_y = batch->state.grab_mash_stick_y_sign[idx];
  if (stick_x < -c->grab_mash_stick_threshold) {
    next_x = -1;
  } else if (stick_x > c->grab_mash_stick_threshold) {
    next_x = 1;
  }
  if (stick_y < -c->grab_mash_stick_threshold) {
    next_y = -1;
  } else if (stick_y > c->grab_mash_stick_threshold) {
    next_y = 1;
  }
  if (batch->state.grab_mash_stick_x_sign[idx] != next_x ||
      batch->state.grab_mash_stick_y_sign[idx] != next_y) {
    batch->state.grab_mash_stick_x_sign[idx] = next_x;
    batch->state.grab_mash_stick_y_sign[idx] = next_y;
    return 1u;
  }
  return 0u;
}

static inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline uint8_t guard_try_enter_jump_oos(MslBatch* batch, const MslCommonParams* c,
                                               size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Guard jump OoS decomp path is ftCo_800CB024:
  // - ftCo_Jump_CheckInput (tap jump or XY),
  // - then c-stick-up check (ftCo_800DF910).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA,ftCo_GuardOff_IASA
  // }
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
  // refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF910
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  MslJumpInput jump_input = MSL_JUMP_INPUT_NONE;
  if (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) {
    jump_input = MSL_JUMP_INPUT_LSTICK;
  } else if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) {
    jump_input = MSL_JUMP_INPUT_XY;
  } else if (cstick_y >= c->tap_jump_threshold) {
    jump_input = MSL_JUMP_INPUT_CSTICK;
  }
  if (jump_input == MSL_JUMP_INPUT_NONE) {
    return 0;
  }

  batch->state.action_id[idx] = (uint16_t)MSL_ACT_KNEE_BEND;
  batch->state.animation_index[idx] = (uint32_t)MSL_SM_KNEE_BEND;
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  batch->state.kneebend_jump_input[idx] = (uint8_t)jump_input;
  batch->state.kneebend_is_short_hop[idx] = 0;
  batch->state.guard_jump_oos_entered_this_frame[idx] = 1u;
  return 1;
}

static inline uint8_t guard_jump_oos_has_input(const MslBatch* batch, const MslCommonParams* c,
                                               size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  const float stick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float cstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y);
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];
  if (stick_y >= c->tap_jump_threshold && tilt_timer_y < c->tap_jump_tilt_max_frames) {
    return 1u;
  }
  if ((buttons_pressed & (uint16_t)MSL_BUTTON_XY) != 0) {
    return 1u;
  }
  if (cstick_y >= c->tap_jump_threshold) {
    return 1u;
  }
  return 0u;
}

static inline uint8_t guard_try_enter_iasa_defense(MslBatch* batch, const MslCommonParams* c,
                                                   size_t idx) {
  if (batch == NULL || c == NULL) {
    return 0;
  }
  // Decomp GuardOn/Guard/GuardReflect IASA order:
  // item throw -> spotdodge -> roll -> catch -> jump -> taunt.
  // This sim currently models the shield-defense subset (spotdodge/roll + catch + jump) in this order.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_IASA,ftCo_Guard_IASA,ftCo_GuardReflect_IASA
  // }
  if (escape_try_enter_from_guard(batch, c, idx)) {
    return 1;
  }
  if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
    return 1;
  }
  if (guard_try_enter_jump_oos(batch, c, idx)) {
    return 1;
  }
  return 0;
}

static inline uint8_t apply_shield_hold_drain(MslBatch* batch, const MslCommonParams* c, size_t idx,
                                              float trig_unit,
                                              uint8_t preserve_lightshield_amount) {
  // Decomp (GALE01): ftCo_800925A4 updates fp->lightshield_amount with a negative-input latch and
  // drains shield HP using the resulting value.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
  const float denom = 1.0f - c->trigger_deadzone;
  if (!(denom > 0.0f)) {
    return 0u;
  }
  float light = batch->state.lightshield_amount[idx];
  if (!preserve_lightshield_amount) {
    const float t = (trig_unit - c->trigger_deadzone) / denom;
    if (t >= 0.0f) {
      light = clamp01(t);
    }
  }
  batch->state.lightshield_amount[idx] = light;
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;

  float hp = batch->state.shield_hp[idx];
  hp -= drain;
  if (hp < 0.0f) {
    hp = 0.0f;
    batch->state.shield_hp[idx] = hp;
    return 1u;
  }
  batch->state.shield_hp[idx] = hp;
  return 0u;
}

static inline uint8_t apply_shield_hold_drain_preserve_drain_refresh_store(MslBatch* batch,
                                                                           const MslCommonParams* c,
                                                                           size_t idx,
                                                                           float trig_unit) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const float denom = 1.0f - c->trigger_deadzone;
  const float drain_light = batch->state.lightshield_amount[idx];
  float store_light = drain_light;
  if (denom > 0.0f) {
    const float t = (trig_unit - c->trigger_deadzone) / denom;
    if (t >= 0.0f) {
      store_light = clamp01(t);
    }
  }
  batch->state.lightshield_amount[idx] = store_light;
  const float drain_factor =
      (drain_light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) +
      c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;
  float hp = batch->state.shield_hp[idx] - drain;
  if (hp < 0.0f) {
    hp = 0.0f;
    batch->state.shield_hp[idx] = hp;
    return 1u;
  }
  batch->state.shield_hp[idx] = hp;
  return 0u;
}

void guard_update_shield_recharge(MslBatch* batch, const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  // Empirically (and in replays), shield recharge can happen during GuardOff; the source gate is
  // the live shield-active bit, resolved by the shared Guard lifecycle helper.
  if (msl_guard_lifecycle_blocks_shield_recharge(batch, idx)) {
    return;
  }
  msl_guard_lifecycle_apply_shield_recharge(batch, c, idx);
}

static inline void action_update_shield_recharge_post_state(MslBatch* batch,
                                                            const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      guard_update_shield_recharge(batch, c, idx);
    }
  }
}

static inline void guard_update_grounded_anim_callback_pre_input(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];

  // GuardReflect/GuardSetOff anim-callback timing (prio 1):
  // - ftCo_GuardReflect_Anim calls ftCo_80093BC0 (x14/x18 tick + expire clears), then GuardOn_Anim.
  // - ftCo_GuardSetOff_Anim also calls ftCo_80093BC0 while shieldstun anim owns GuardDesc state.
  // - Fighter_8006A360 runs this under !hitlag.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT || a0 == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    if (batch->state.hitlag_started_frame[idx] == 0) {
      uint8_t t14 = batch->state.guard_reflect_timer_x14[idx];
      if (t14 > 0) {
        t14--;
        batch->state.guard_reflect_timer_x14[idx] = t14;
      }
      if (t14 == 0u && batch->state.hitlag_pre_timer[idx] == 0u) {
        // Decomp: ftCo_80093BC0 clears x221C_b1 when the shorter x14 reflect descriptor timer
        // expires, matching the x18/x221C_b2 clear below.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
        const size_t flags_i =
            idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
        batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
      }
      uint8_t t18 = batch->state.guard_reflect_timer_x18[idx];
      if (t18 > 0) {
        t18--;
        batch->state.guard_reflect_timer_x18[idx] = t18;
        if (t18 == 0u) {
          // Decomp: when mv.co.guard.x18 expires in ftCo_80093BC0, x221C_b2 is cleared in the
          // same GuardReflect_Anim callback pass.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
          const size_t flags_i =
              idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
          batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
      }
    }
  } else {
    // Keep GuardReflect timers strictly callback-owner action-scoped to avoid stale seeded carryover.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardReflect_Anim,ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    batch->state.guard_reflect_timer_x14[idx] = 0;
    batch->state.guard_reflect_timer_x18[idx] = 0;
    batch->state.guard_reflect_origin_guardon[idx] = 0u;
  }
}

static inline void rebound_update_anim_callback_pre_input(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const uint16_t a0 = batch->state.action_id[idx];
  if (a0 != (uint16_t)MSL_ACT_REBOUND_STOP && a0 != (uint16_t)MSL_ACT_REBOUND) {
    return;
  }
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }

  float rebound_anim_speed = msl_f32_from_q16_16(batch->state.rebound_anim_rate_fp_q16_16[idx]);
  if (!(rebound_anim_speed > 0.0f)) {
    rebound_anim_speed = 1.0f;
  }
  if (c != NULL && ch != NULL) {
    const float source_x0 = (batch->state.rebound_ground_accel_2[idx] != 0.0f)
                                ? batch->state.rebound_ground_accel_2[idx]
                                : batch->state.speed_ground_x_self[idx];
    const float rebound_speed_abs = msl_absf(source_x0);
    // Rebound anim-rate ownership:
    // - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
    // - Runtime clank entry carries that exact hidden rate. Replay-facing ReboundStop hitlag-tail
    //   seeds may still only have xE8, so fall back to reconstructing x191C from the queued
    //   `mv.co.rebound.x0` lane before it is consumed.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
    // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
    if (batch->state.rebound_anim_rate_fp_q16_16[idx] <= 0 && c->rebound_ground_x0_mul > 0.0f &&
        rebound_speed_abs > c->rebound_ground_x0_base) {
      const float rebound_x191c =
          (rebound_speed_abs - c->rebound_ground_x0_base) / c->rebound_ground_x0_mul;
      if (rebound_x191c > 0.0f) {
        rebound_anim_speed = (ch->rebound_anim_numerator_frames + 0.1f) / rebound_x191c;
      }
    }
  }

  if (a0 == (uint16_t)MSL_ACT_REBOUND_STOP) {
    // ReboundStop_Anim callback ownership:
    // - ftCo_ReboundStop_Anim immediately calls ftCo_80099E44.
    // - ftCo_80099E44 enters Rebound through Fighter_ChangeMotionState.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
    //   ftCo_ReboundStop_Anim,ftCo_80099E44
    // }
    // refs/melee/src/melee/ft/chara/ftCommon/forward.h::{
    //   ftCo_MS_ReboundStop,ftCo_MS_Rebound,ftCo_SM_Rebound
    // }
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_REBOUND;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_REBOUND;
    msl_anim_timebase_enter(batch, idx, 0.0f, rebound_anim_speed);
    batch->state.rebound_anim_rate_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_anim_speed);
    return;
  }

  if (batch->state.action_frame[idx] == 0 && rebound_anim_speed > 0.0f &&
      batch->state.rebound_anim_rate_fp_q16_16[idx] > 0) {
    batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_anim_speed);
  }

  // Rebound_Anim callback ownership:
  // - Rebound ends through ft_8008A2BC (Wait enter) when the submotion has no frames remaining.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_Rebound_Anim
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_REBOUND);
  if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
    rebound_wait_restore_ground_from_carried_floor(batch, idx);
    escape_enter_wait(batch, idx);
  }
}

void guard_update_grounded(MslBatch* batch, const MslCommonParams* c, size_t idx,
                           uint8_t allow_entry) {
  if (batch == NULL || c == NULL) {
    return;
  }

  const uint16_t a0 = batch->state.action_id[idx];
  const float a0_anim_frame = batch->state.anim_frame_f32[idx];
  batch->state.guard_x10_frame_start[idx] = batch->state.guard_x10[idx];
  if (a0 != (uint16_t)MSL_ACT_GUARD_ON) {
    batch->state.guard_on_cliff_end_source[idx] = 0u;
  }
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  const uint8_t guard_on_fresh_entry_from_non_shield_snapshot =
      // Decomp ownership: input callbacks run once per fighter per frame (Fighter_procUpdate).
      // When Wait/Damage IASA enters GuardOn via ftCo_80091A4C -> ftCo_800924C0, GuardOn_IASA must
      // not consume the same input again in that frame.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0,ftCo_GuardOn_IASA}
      // Scope gate: GuardOn entry from a non-shield owner has already consumed this frame's
      // callback lane, so suppress immediate re-consume regardless of jump-button edge source.
      //
      // This now also covers the grounded Damage_IASA Z-bridge in src/knockdown.c; keeping the
      // broader fresh-entry suppression is decomp-shaped once the earlier Wait_IASA attack owners
      // are modeled ahead of guard in that grounded damage subset.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
      (a0 == (uint16_t)MSL_ACT_GUARD_ON && batch->state.action_frame[idx] < 0 &&
       batch->state.animation_index[idx] == 0xFFFFFFFFu &&
       batch->state.guard_on_entered_this_frame[idx] != 0u &&
       !msl_guard_lifecycle_action_has_shield_callback(batch->state.prev_action_id[idx]))
          ? 1u
          : 0u;
  const uint8_t guardreflect_fresh_entry_from_this_callback =
      // Source ordering: GuardReflect entry helpers are reached from IASA during
      // Fighter_procUpdate, after Fighter_8006A360 has already run this frame's Anim callback.
      // If an earlier simulated owner in this same input-callback pass entered GuardReflect, do
      // not let a later shared guard pass immediately run GuardReflect_Anim/GuardOn_Anim and drain
      // shield HP one source frame early. Seeded replay rows keep this marker clear, so their
      // normal next-frame GuardReflect drain still runs.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_8009388C,ftCo_80093A50,ftCo_GuardReflect_Anim,ftCo_800925A4}
      (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[idx] < 0 &&
       batch->state.animation_index[idx] == 0xFFFFFFFFu &&
       batch->state.guard_reflect_entered_this_frame[idx] != 0u)
          ? 1u
          : 0u;

  if (!msl_guard_lifecycle_action_has_shield_callback(a0)) {
    batch->state.guard_release_latched_xc[idx] = 0;
    batch->state.guard_x10[idx] = 0;
    batch->state.lightshield_amount[idx] = 0.0f;
    batch->state.guard_entry_via_wait_callback[idx] = 0u;
    batch->state.guard_on_cliff_end_source[idx] = 0u;
    batch->state.guard_entry_via_dash_91ad8[idx] = 0u;
  }
  batch->state.guard_reflect_entry_dash_terminal_scalar[idx] = 0u;

  const float trig = guard_x650_from_input(c, batch->state.input_buttons[idx],
                                           batch->state.input_l[idx], batch->state.input_r[idx]);
  const float anim_trig =
      guard_x650_from_input(c, batch->state.prev_input_buttons[idx], batch->state.prev_input_l[idx],
                            batch->state.prev_input_r[idx]);
  // Decomp uses held_inputs & HSD_PAD_LR for guard entry/release ownership.
  // Keep this aligned with the input owner that builds the sim's LR-held lane:
  // - digital L/R,
  // - trigger past the common deadzone,
  // - Z-mapped LR lane used by input.x668 / held_inputs plumbing.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092BCC}
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10_Inner1
  // refs/melee/src/melee/ft/fighter.c:1868-1890
  const uint16_t held_buttons = batch->state.input_buttons[idx];
  const uint8_t shield_held_inputs =
      (((held_buttons & (uint16_t)(LR | MSL_BUTTON_Z)) != 0u) || (trig > c->trigger_deadzone)) ? 1u
                                                                                               : 0u;
  const uint8_t guard_anim_held_shield_x650_transition =
      // Source ordering boundary:
      // GuardOn/Guard Anim can consume the frame-start `input.x650` for the shield-hold drain before
      // the same frame's input callback observes a shield trigger transition such as Z->L, L->Z, or
      // no-trigger -> hard analog while the no-submotion Guard lifecycle is still active. Scope this
      // to rows where shield remains held on the current sample; release-to-none rows keep the
      // seed/current trigger surface so a stale previous L sample cannot recreate hard-shield
      // geometry.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_800925A4}
      (shield_held_inputs && batch->state.animation_index[idx] == UINT32_MAX &&
       batch->state.action_frame[idx] < 0 && anim_trig != trig)
          ? 1u
          : 0u;
  const float guard_drain_trig = guard_anim_held_shield_x650_transition ? anim_trig : trig;
  const uint8_t guard_x10_seed = batch->state.guard_x10[idx];

  // Guard release lockout (mv.co.guard.xC + mv.co.guard.x10) is modeled explicitly and seeded via
  // replay-history preprocessing (Slippi does not expose move vars directly).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC (xC latch)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4 (x10 tick)

  // GuardSetOff (shieldstun): no IASA until the underlying "GuardDamage" animation completes.
  //
  // Decomp:
  // - Enter: ftCo_80092F2C (sets anim rate based on shieldstun duration).
  // - Update/exit: ftCo_GuardSetOff_Anim transitions to Guard or GuardOff when the animation ends.
  // - Motion-state table selects ftCo_SM_GuardDamage as the submotion for GuardSetOff.
  //   refs/melee/src/melee/ft/ftmotionstates.c (GuardSetOff entry uses ftCo_SM_GuardDamage).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
  if (a0 == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
    if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
      // Shieldstun over:
      // - If mv.co.guard.xC is latched, transition to GuardOff (ftCo_80092BE8 -> ftCo_80092C54).
      // - Else transition to Guard (ftCo_800928CC).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BE8
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800928CC
      if (batch->state.guard_release_latched_xc[idx]) {
        enter_guard_off(batch, idx);
        return;
      }

      // Shieldstun over -> return to Guard (hold).
      enter_guard_hold(batch, idx);
      // IASA for the newly-entered Guard state in the same frame.
      //
      // GuardSetOff_Anim runs in Fighter_8006A360 (prio 1). If it enters Guard, the later
      // Fighter_procUpdate input callback dispatches Guard_IASA in the destination state. Guard_IASA
      // first calls inlineC0, which latches mv.co.guard.xC from current held_inputs and exits to
      // GuardOff when x10 is already clear.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_Guard_IASA,inlineC0,ftCo_80092BCC}
      const uint8_t guard_x10_before_destination_iasa = batch->state.guard_x10[idx];
      if (!shield_held_inputs) {
        batch->state.guard_release_latched_xc[idx] = 1u;
      }
      if (batch->state.guard_release_latched_xc[idx] && guard_x10_before_destination_iasa == 0u) {
        enter_guard_off(batch, idx);
        return;
      }
      if (guard_try_enter_iasa_defense(batch, c, idx)) {
        return;
      }
      // Replay-visible GuardSetOff -> Guard carry rows normally expose the destination Guard
      // lockout timer after the first Guard callback phase, but without the Guard shield-hold HP
      // drain on the transition row. Model that hidden x10 handoff here so the later `inlineC0`
      // release gate observes the same countdown as the source sequence.
      //
      // GuardSetOff shieldstun-exit carry publishes the destination Guard row after the first
      // GuardSetOff -> Guard callback handoff. Fresh same-frame shield entries initialize x10 from
      // raw p_ftCommonData->x268 at shield-hit entry; carried GuardSetOff rows use their seeded
      // value. Both paths consume the ordinary carry tick here before the next Guard_IASA release
      // gate.
      //
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_Guard_IASA,ftCo_800925A4,inlineC0}
      if (batch->state.guard_x10[idx] > 0u) {
        batch->state.guard_x10[idx] = (uint8_t)(batch->state.guard_x10[idx] - 1u);
      }
      return;
    }
    return;
  }

  // ----------------
  // Guard state loop
  // ----------------
  if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD || a0 == MSL_ACT_GUARD_REFLECT) {
    batch->state.animation_index[idx] = 0xFFFFFFFFu;
    const uint8_t can_update = (batch->state.hitlag_started_frame[idx] == 0) ? 1 : 0;
    uint8_t guard_reflect_from_guard_pending = 0u;
    if (guard_on_fresh_entry_from_non_shield_snapshot ||
        guardreflect_fresh_entry_from_this_callback) {
      return;
    }

    if (can_update) {
      // Powershield / GuardReflect entry (while guarding).
      //
      // Decomp: ftCo_80093694:
      //   if (fp->mv.co.guard.x0 < p_ftCommonData->x2A0 &&
      //       fp->input.x668 & (HSD_PAD_R | HSD_PAD_L) &&
      //       fp->x672_input_timer_counter < p_ftCommonData->x2A0)
      //     ftCo_80093850(gobj);
      //
      // No-submotion GuardOn entry snapshots expose two independent hidden lanes:
      // - current GuardOn with non-shield `seed_prev_action_id`, negative action_frame, and no
      //   submotion is still in the hidden guard.x0 entry window before replay publishes a positive
      //   action_frame.
      // - only the generated grounded-locomotion IASA subset also owns the x672 frame-start
      //   replay-seed boundary before the persistent trigger timer advances for the next frame.
      // Other source GuardOn handoffs retain current x672 ownership. Steady GuardOn snapshots
      // (seed_prev is already shield-owned) keep both ordinary lanes so held-shield rows do not
      // re-enter GuardReflect repeatedly.
      // Scope gate: this check is in ftCo_GuardOn_IASA only (not ftCo_Guard_IASA), so only
      // GuardOn can re-enter GuardReflect through this path.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      //
      // Snapshot note: in-suite Slippi seeds can carry `action_frame < 0` when
      // `animation_index==0xFFFFFFFF`. For this *guard.x0* gate only, treat negative action_frame
      // as 0 (entry-like) rather than a large/underflowed value; this preserves teacher-forced
      // prefix-invariant powershield behavior without using replay-fit heuristics.
      // The frame-start x672 replay reconstruction is valid for the source-backed fresh
      // grounded-locomotion entry shape (`x672<=1` at the seed snapshot). Do not gate this on
      // shield HP: ftCo_80093694 does not read shield health, and fresh GuardOn can occur after
      // prior shield damage.
      const uint8_t guardon_entry_x0_nonshield_seed =
          (a0 == (uint16_t)MSL_ACT_GUARD_ON && batch->state.action_frame[idx] < 0 &&
           batch->state.animation_index[idx] == UINT32_MAX &&
           batch->state.x672_input_timer_frame_start[idx] <= 1u &&
           !msl_guard_lifecycle_action_has_shield_callback(batch->state.seed_prev_action_id[idx]))
              ? 1u
              : 0u;
      const uint8_t guardon_frame_start_x672_seed =
          (guardon_entry_x0_nonshield_seed != 0u &&
           // MSLMSO01 separates this frame-start x672 powershield bridge from the broader fresh
           // GuardOn item ShieldDesc owner. Landing_IASA can publish ShieldDesc, but its follow-up
           // GuardOn_IASA consumes live x672 rather than this replay frame-start lane.
           // data/motion_state/owners/*.bin::MSLMSO01 GUARDON_FRAME_START_X672_IASA
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093694
           msl_motion_state_common_class_has_fast(batch->state.seed_prev_action_id[idx],
                                                  MSL_MS_CLASS_GUARDON_FRAME_START_X672_IASA))
              ? 1u
              : 0u;
      const uint16_t guard_x0 = (batch->state.action_frame[idx] < 0)
                                    ? (guardon_entry_x0_nonshield_seed != 0u
                                           ? 0u
                                           : (uint16_t)c->powershield_reflect_window_frames)
                                    : (uint16_t)batch->state.action_frame[idx];
      const uint8_t guardon_x672_for_reflect = guardon_frame_start_x672_seed
                                                   ? batch->state.x672_input_timer_frame_start[idx]
                                                   : batch->state.x672_input_timer[idx];
      if (a0 == (uint16_t)MSL_ACT_GUARD_ON &&
          guard_x0 < (uint16_t)c->powershield_reflect_window_frames &&
          (batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0 &&
          guardon_x672_for_reflect < c->powershield_reflect_window_frames) {
        guard_reflect_from_guard_pending = 1u;
      }

      // Decomp ordering note (GuardOn/Guard discrete cluster):
      // - mv.co.guard.x10 is decremented inside ftCo_800925A4 (called by GuardOn_Anim / Guard_Anim).
      // - The GuardOff transition gate (xC && !x10) lives in inlineC0, called by GuardOn_IASA / Guard_IASA.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      //
      // Our step ordering models IASA before the x10 decrement, so the GuardOff check must use the
      // pre-decrement x10 value; otherwise GuardOn can drop 1 frame early when x10 transitions 1->0.
      const uint8_t x10_pre = batch->state.guard_x10[idx];
      const uint8_t guard_no_submotion_snapshot =
          (a0 == (uint16_t)MSL_ACT_GUARD && batch->state.action_frame[idx] < 0 &&
           batch->state.animation_index[idx] == 0xFFFFFFFFu &&
           batch->state.anim_frame_f32[idx] < 0.0f)
              ? 1u
              : 0u;
      const uint8_t guard_setoff_carry_snapshot =
          // Restrict the no-submotion carry suppression lane to true GuardSetOff->Guard carry.
          // A plain Guard hold snapshot can share (anim=-1, frame_speed>0, x672=0xFE) after
          // powershield entry; suppressing release there incorrectly blocks GuardOff on LR release.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}
          (guard_no_submotion_snapshot &&
           batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
           msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]) > 0.0f &&
           batch->state.x672_input_timer[idx] == 0xFEu)
              ? 1u
              : 0u;
      // Guard release latch ownership (ftCo_80092BCC):
      // - level check: if (!(held_inputs & HSD_PAD_LR)) xC = true.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
      if (!shield_held_inputs) {
        batch->state.guard_release_latched_xc[idx] = 1;
      }
      // Decomp: ftCo_800925A4 updates lightshield_amount + drains shield HP + decrements x10 while
      // the shield is active (fp->x221B_b0). Approximate shield-active as (shield_hp > 0).
      const uint8_t guardreflect_active_timer_no_submotion =
          (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[idx] < 0 &&
           batch->state.animation_index[idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14[idx] > 0u &&
           (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON))
              ? 1u
              : 0u;
      if (batch->state.shield_hp[idx] > 0.0f && !guardreflect_active_timer_no_submotion) {
        const uint8_t guard_jump_pending = guard_jump_oos_has_input(batch, c, idx) ? 1u : 0u;
        const uint8_t guardon_no_submotion_snapshot =
            (a0 == (uint16_t)MSL_ACT_GUARD_ON && batch->state.animation_index[idx] == 0xFFFFFFFFu &&
             batch->state.action_frame[idx] < 0)
                ? 1u
                : 0u;
        const uint8_t guardreflect_terminal_no_submotion_snapshot =
            (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.animation_index[idx] == 0xFFFFFFFFu &&
             batch->state.action_frame[idx] <= -2 &&
             batch->state.guard_reflect_timer_x14[idx] == 0u &&
             batch->state.guard_reflect_timer_x18[idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t guard_exit_to_guard_off_pending =
            (!guard_setoff_carry_snapshot && batch->state.guard_release_latched_xc[idx] &&
             x10_pre == 0)
                ? 1u
                : 0u;
        const uint8_t guard_snapshot_spotdodge_pending =
            (guardon_no_submotion_snapshot && batch->state.guard_entry_via_wait_callback[idx] &&
             !guard_exit_to_guard_off_pending && escape_guard_wants_spotdodge(batch, c, idx))
                ? 1u
                : 0u;
        const uint8_t guard_snapshot_refresh_drain_split =
            (guardreflect_terminal_no_submotion_snapshot && guard_drain_trig > c->trigger_deadzone)
                ? 1u
                : 0u;
        // Decomp timing note:
        // - GuardOn/Guard Anim drains shield through ftCo_800925A4 before the same frame's
        //   IASA callback can consume jump OoS via ftCo_800CB024.
        // - Fighter_8006A360 runs Anim before Fighter_Spaghetti_8006AD10 installs the next
        //   controller sample for IASA. The shield hold drain therefore consumes the frame-start
        //   `input.x650` (our prev_input_* lanes), while release/IASA gates below use the current
        //   held input.
        // - Ordinary GuardOn/Guard snapshots still follow ftCo_800925A4 order: refresh
        //   `lightshield_amount` from Anim-visible input.x650 first, then drain using that value.
        // - Expired no-submotion GuardReflect terminal rows run the same GuardOn_Anim drain before
        //   transitioning to Guard through ftCo_800928CC. That terminal bridge preserves the carried
        //   lightshield owner for this row's drain, then refreshes the stored value for following
        //   Guard rows.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_800921DC,ftCo_800925A4,ftCo_80093BC0,ftCo_GuardReflect_Anim,
        //   ftCo_GuardOn_Anim,ftCo_Guard_Anim,ftCo_GuardOn_IASA,ftCo_Guard_IASA
        // }
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
        // Runtime-only distinction: when a frozen GuardOn snapshot came from a same-frame
        // `... -> Wait -> GuardOn` callback handoff and immediately spotdodges, vanilla keeps
        // shield HP unchanged on the first EscapeN frame. Preserve that by skipping the GuardOn
        // drain on just that handoff family.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800924C0,ftCo_800925A4,ftCo_GuardOn_IASA}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
        uint8_t shield_break_pending = 0u;
        if (guard_snapshot_spotdodge_pending) {
          // no-op
        } else if (guard_snapshot_refresh_drain_split) {
          shield_break_pending =
              apply_shield_hold_drain_preserve_drain_refresh_store(batch, c, idx, guard_drain_trig);
        } else {
          shield_break_pending =
              apply_shield_hold_drain(batch, c, idx, guard_drain_trig, guard_jump_pending);
        }
        if (shield_break_pending) {
          enter_shield_break_fly(batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
          return;
        }
      }
      // Consume the source "entered GuardOn through this callback family" marker after the first
      // immediate GuardOn/spotdodge handoff window. Keeping it longer stale-carries the entry owner
      // into unrelated later GuardOn_IASA rows.
      if (batch->state.guard_entry_via_wait_callback[idx] > 0u) {
        batch->state.guard_entry_via_wait_callback[idx] =
            (uint8_t)(batch->state.guard_entry_via_wait_callback[idx] - 1u);
      }

      // Decomp: Guard IASA exits to GuardOff only once (xC && x10==0).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      const uint8_t guardon_no_submotion_release_x10_cleared_by_anim =
          // Source order is GuardOn_Anim before GuardOn_IASA. On no-submotion GuardOn snapshots,
          // the frame-start release latch can coexist with x10==1; when a same-frame jump input is
          // present, source ticks x10 to zero in GuardOn_Anim and inlineC0 exits to GuardOff before
          // ftCo_800CB024 can consume that jump press. Keep the older pre-decrement x10 gate for
          // ordinary release countdown rows with no competing jump input.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardOn_Anim,ftCo_800925A4,inlineC0,ftCo_GuardOn_IASA}
          (a0 == (uint16_t)MSL_ACT_GUARD_ON && batch->state.animation_index[idx] == UINT32_MAX &&
           batch->state.action_frame[idx] < 0 && !shield_held_inputs &&
           batch->state.guard_release_latched_xc[idx] && x10_pre == 1u &&
           guard_jump_oos_has_input(batch, c, idx) != 0u)
              ? 1u
              : 0u;
      if (batch->state.guard_release_latched_xc[idx] &&
          (x10_pre == 0 || guardon_no_submotion_release_x10_cleared_by_anim)) {
        if (a0 == (uint16_t)MSL_ACT_GUARD_ON) {
          // Seed-snapshot bridge for GuardOn no-submotion rows:
          // - GALE01 ordering is GuardOn_Anim then GuardOn_IASA.
          // - On snapshot-shaped GuardOn seeds (animation_index=-1, state_age=-1), this can appear
          //   as GuardOn -> Guard -> GuardOff in one frame when release gate fires, consuming two
          //   motion-state entry bundles before the final GuardOff output.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_800928CC,ftCo_80092C54}
          const uint8_t guard_no_submotion_snapshot =
              (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
               batch->state.anim_frame_f32[idx] < 0.0f)
                  ? 1u
                  : 0u;
          if (guard_no_submotion_snapshot && guard_x10_seed == 0) {
            enter_guard_hold(batch, idx);
          }
        }
        if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
          batch->state.guard_reflect_timer_x14[idx] = 0;
          batch->state.guard_reflect_timer_x18[idx] = 0;
          // ftCo_GuardReflect_Anim chains into the GuardOn_Anim body after ftCo_80093BC0, so the
          // same release gate passes through ftCo_800928CC (Guard) before ftCo_80092C54 reaches
          // GuardOff — two motion-state entry bundles, exactly like the GuardOn arm above. The
          // shared plAttack_80037B08 instance counter advances twice on this boundary; collapsing
          // it to a single direct GuardOff entry desyncs both players' instance ids for the rest
          // of the rollout (MAJ rec302: vanilla ids 85+86 consumed on this exact exit).
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800928CC,ftCo_80092C54}
          // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
          const uint8_t guard_reflect_no_submotion_snapshot =
              (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
               batch->state.anim_frame_f32[idx] < 0.0f)
                  ? 1u
                  : 0u;
          if (guard_reflect_no_submotion_snapshot) {
            enter_guard_hold(batch, idx);
          }
        }
        enter_guard_off(batch, idx);
        return;
      }

      // Decomp: inlineC0 decrements mv.co.guard.x1C only when GuardOn/Guard/GuardReflect IASA
      // does not exit to GuardOff. GuardOff_IASA then uses the non-zero timer to allow the full
      // special/attack chain after a powershield shield contact.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineC0,ftCo_GuardOff_IASA}
      if (batch->state.guard_special_enable_timer_x1c[idx] > 0u) {
        batch->state.guard_special_enable_timer_x1c[idx] =
            (uint8_t)(batch->state.guard_special_enable_timer_x1c[idx] - 1u);
      }

      if (x10_pre > 0 && batch->state.shield_hp[idx] > 0.0f) {
        batch->state.guard_x10[idx] = (uint8_t)(x10_pre - 1u);
      }
    }

    // GuardOn/GuardReflect -> Guard when the GuardOn "raise shield" window completes.
    //
    // Decomp: ftCo_GuardOn_Anim increments mv.co.guard.x0 and transitions when x0 >= fp->x2E8.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_Anim
    //
    // Teacher-forced reseed note:
    // In-suite Slippi post-frames frequently seed GuardOn with `animation_index==0xFFFFFFFF` and
    // `state_age==-1` (so this sim's derived anim/action_frame cannot represent mv.co.guard.x0).
    // We therefore add a decomp-anchored, reseed-friendly fallback:
    // - for GuardOn, when mv.co.guard.x10 is already 0 and shield is still held, treat GuardOn as
    //   complete and enter Guard;
    // - for GuardReflect, same fallback once the reflect window timer (mv.co.guard.x14) has expired,
    //   since ftCo_GuardReflect_Anim chains into GuardOn_Anim after ftCo_80093BC0.
    //
    // This preserves deterministic one-step GuardOn->Guard transitions without replay-fit constants
    // and keeps the normal anim-end gate in place when a real timebase is available.
    const uint8_t guard_no_submotion_snapshot = (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
                                                 batch->state.anim_frame_f32[idx] < 0.0f)
                                                    ? 1u
                                                    : 0u;
    const uint8_t guard_reflect_window_expired =
        (batch->state.guard_reflect_timer_x14[idx] == 0u) ? 1u : 0u;
    // Snapshot bridge (GuardReflect negative lane):
    // - Replay snapshots can land on GuardReflect with no submotion (anim=-1) and action_frame<=-2.
    // - In this lane, reflect timers can already be expired while mv.co.guard.x10 still reflects a
    //   stale release-lockout seed, and GALE01 callback ordering at this boundary can advance to
    //   Guard before the next "normal" x10 gate observation.
    // - Keep the usual x10 gate for GuardOn and GuardReflect's normal lane; only bypass x10 for
    //   GuardReflect no-submotion rows at action_frame<=-2 with expired reflect timer.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800928CC}
    const uint8_t guard_reflect_snapshot_neg_lane =
        (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[idx] <= -2 &&
         batch->state.guard_reflect_timer_x14[idx] == 0u &&
         batch->state.guard_reflect_timer_x18[idx] == 0u)
            ? 1u
            : 0u;
    const uint8_t guard_reflect_snapshot_neg_lane_x10_one =
        (guard_reflect_snapshot_neg_lane && guard_x10_seed == 1u) ? 1u : 0u;
    const uint8_t guard_reflect_released_terminal_hold_snapshot =
        // Replay-visible GuardReflect snapshots can sit on the terminal no-submotion frame with
        // reflect timers expired and mv.co.guard.x10 about to clear. GALE01 exposes this as a
        // Guard post-frame before the following GuardOff; consuming destination Guard_IASA in the
        // same snapshot row exits one frame too early.
        //
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800928CC}
        (guard_reflect_snapshot_neg_lane && guard_x10_seed == 1u && !shield_held_inputs) ? 1u : 0u;
    const uint8_t guard_snapshot_hold_fallback =
        (guard_no_submotion_snapshot && shield_held_inputs &&
         ((a0 == (uint16_t)MSL_ACT_GUARD_ON && guard_x10_seed == 0) ||
          (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT && guard_reflect_window_expired &&
           (guard_x10_seed == 0 || guard_reflect_snapshot_neg_lane_x10_one))))
            ? 1u
            : 0u;
    if (batch->state.hitlag_started_frame[idx] == 0 &&
        guard_reflect_released_terminal_hold_snapshot) {
      batch->state.guard_reflect_timer_x14[idx] = 0;
      batch->state.guard_reflect_timer_x18[idx] = 0;
      enter_guard_hold(batch, idx);
      return;
    }
    if (batch->state.hitlag_started_frame[idx] == 0 && guard_snapshot_hold_fallback) {
      // Decomp ordering: GuardReflect_Anim can transition to Guard before input callback dispatch,
      // and the destination Guard_IASA still consumes OoS options in the same frame.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_IASA,ftCo_Guard_IASA}
      if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        batch->state.guard_reflect_timer_x14[idx] = 0;
        batch->state.guard_reflect_timer_x18[idx] = 0;
      }
      enter_guard_hold(batch, idx);
      if (guard_try_enter_iasa_defense(batch, c, idx)) {
        return;
      }
      return;
    }

    // GuardOn/GuardReflect -> Guard when the GuardOn animation finishes.
    // Decomp: ftCo_GuardOn_Anim transitions to ftCo_800928CC when mv.co.guard.x0 >= fp->x2E8.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:367-377.
    //
    // Approximation mapping:
    // - Treat `action_frame` as `mv.co.guard.x0` (both tick once per frame outside hitlag).
    // - Treat `msl_anim_end_frame(char, ftCo_SM_GuardOn)` as `fp->x2E8` (ISO-derived anim timeline length).
    if (a0 == MSL_ACT_GUARD_ON || a0 == MSL_ACT_GUARD_REFLECT) {
      const float end_frame =
          msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_ON);
      if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
        if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
          batch->state.guard_reflect_timer_x14[idx] = 0;
          batch->state.guard_reflect_timer_x18[idx] = 0;
        }
        enter_guard_hold(batch, idx);
      }
    }

    if (guard_reflect_from_guard_pending &&
        batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_ON) {
      // Decomp callback order is GuardOn_Anim then GuardOn_IASA, so same-frame GuardReflect entry
      // from GuardOn must observe the already-applied GuardOn drain/x10 owner work before IASA
      // consumes the LR edge.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
      enter_guard_reflect_from_guard(batch, c, idx);
      return;
    }

    // Shield defensive options (grounded).
    //
    // Decomp call site: ftCo_GuardOn_IASA / ftCo_Guard_IASA.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:393-409 and :454-469.
    //
    // Minimal OoS subset in decomp IASA order (defensive options): spotdodge/roll then jump.
    if (guard_try_enter_iasa_defense(batch, c, idx)) {
      if (a0 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        batch->state.guard_reflect_timer_x14[idx] = 0;
        batch->state.guard_reflect_timer_x18[idx] = 0;
      }
      return;
    }
    return;
  }

  // GuardOff: wait for animation end then go back to Wait.
  if (a0 == MSL_ACT_GUARD_OFF) {
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_GUARD_OFF;
    // GuardOff IASA: when mv.co.guard.x1C is live, decomp tries the special/attack chain before
    // the spotdodge/jump fallback. Specials are modeled in the later B-special passes, so do not
    // let the fallback consume B-press rows first.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_IASA
    const uint8_t guardoff_special_chain_pending =
        (batch->state.guard_special_enable_timer_x1c[idx] != 0u &&
         (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) != 0u)
            ? 1u
            : 0u;
    // GuardOff fallback IASA: allow spotdodge + jump, but not rolls.
    //
    // Decomp: ftCo_GuardOff_IASA calls spotdodge check (ftCo_8009980C) and jump check (ftCo_800CB024),
    // but does *not* call the roll check (ftCo_8009917C). Allowing EscapeF/B here causes a dominant
    // GuardOff->EscapeB mismatch cluster in teacher-forced one-step eval.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009917C
    if (!guardoff_special_chain_pending) {
      if (escape_try_enter_spotdodge_from_guard(batch, c, idx)) {
        return;
      }
      if (guard_try_enter_jump_oos(batch, c, idx)) {
        return;
      }
    }
    const float end_frame =
        msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_OFF);
    if (end_frame > 0.0f && (batch->state.anim_frame_f32[idx] >= end_frame)) {
      guard_enter_wait(batch, idx);
      {
        // GuardOff_Anim can enter Wait before this frame's input callback dispatch. If the
        // destination Wait_IASA reaches ftCo_80091A4C, held shield enters GuardOn/GuardReflect on
        // the same source frame. Keep this at the anim-end handoff and require the earlier
        // Wait_IASA command families to be absent so we do not turn attacks, specials, catch, or
        // spotdodge into guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOff_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
        const uint16_t pre_guard_buttons = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B | MSL_BUTTON_Z);
        const uint8_t cstick_command =
            (apply_deadzone(stick_i8_to_unit(batch->state.input_c_x[idx]), c->lstick_deadzone_x) !=
                 0.0f ||
             apply_deadzone(stick_i8_to_unit(batch->state.input_c_y[idx]), c->lstick_deadzone_y) !=
                 0.0f)
                ? 1u
                : 0u;
        const float stick_y =
            apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
        const uint8_t spotdodge_before_guard =
            (stick_y < -c->crouch_stick_threshold && shield_held_inputs) ? 1u : 0u;
        const uint8_t wait_iasa_can_reach_guard =
            (shield_held_inputs && batch->state.shield_hp[idx] > 0.0f &&
             (batch->state.input_buttons[idx] & pre_guard_buttons) == 0u &&
             (batch->state.input_buttons_pressed[idx] & pre_guard_buttons) == 0u &&
             !cstick_command && !spotdodge_before_guard)
                ? 1u
                : 0u;
        if (wait_iasa_can_reach_guard) {
          if ((batch->state.input_buttons_pressed[idx] & (uint16_t)LR) != 0u &&
              batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
            enter_guard_reflect_from_locomotion(batch, c, idx);
          } else {
            enter_guard_on(batch, c, idx, 1u);
          }
        }
      }
      return;
    }
    return;
  }

  // ----------------
  // Guard entry gate
  // ----------------
  if (!allow_entry) {
    return;
  }

  // Dash IASA: early shield-hold forces EscapeF.
  //
  // Decomp:
  // - ftCo_Dash_IASA calls ftCo_80099264 when fp->cur_anim_frame <= p_ftCommonData->x48.
  // - ftCo_80099264 enters EscapeF if (held_inputs & HSD_PAD_LR).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099264
  //
  // Approximation:
  // - Keep decomp ordering/gating: ftCo_80099264 is only reached in Dash IASA early branch
  //   (dash.x4 != 0 && cur_anim_frame <= p_ftCommonData->x44), then checks held_inputs&LR.
  // - Use extracted p_ftCommonData->x44/x48 via common params (dash_iasa_x44/dash_iasa_x48).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  if (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u && shield_held_inputs &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x44 &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x48) {
    enter_escape_roll(batch, idx, (uint16_t)MSL_ACT_ESCAPE_F);
    dash_iasa_apply_root_motion_exit_gr_vel_clamp(
        batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
    dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
    return;
  }

  if (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u &&
      batch->state.anim_frame_f32[idx] <= c->dash_iasa_x44) {
    // Decomp: the early Dash_IASA branch (`dash.x4 != 0 && cur_anim_frame <= x44`) checks
    // SpecialS/item/CatchDash/AttackS4/EscapeF and then reaches block_42 without calling the guard
    // helper. Guard/GuardReflect admission starts in the later Dash_IASA branches. The narrow
    // A+tap-jump case below is the block_42 path after AttackS4_8008C114 misses the side-smash
    // threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    if (dash_iasa_try_enter_a_tap_jump_after_attack_s4_miss(batch, c, idx)) {
      return;
    }
    return;
  }

  if (dash_iasa_try_enter_opposite_checkinput_turn_before_guard(batch, c, idx, a0)) {
    return;
  }

  // Decomp: ftCo_80091A4C (used by grounded locomotion IASA functions like Wait/Walk/Run/Turn).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:57-70 and ftCo_Wait.c:43-66.
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  if ((pressed & (uint16_t)LR) != 0 &&
      batch->state.x672_input_timer[idx] < c->powershield_reflect_window_frames) {
    enter_guard_reflect_from_locomotion(batch, c, idx);
    if (dash_iasa_guard_admission_reaches_terminal_scalar(batch, c, idx, a0, a0_anim_frame)) {
      dash_iasa_apply_root_motion_exit_gr_vel_clamp(
          batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
      dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
      const float frame_step = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[idx]);
      batch->state.guard_reflect_entry_dash_terminal_scalar[idx] =
          (a0 == (uint16_t)MSL_ACT_DASH && a0_anim_frame <= (c->dash_iasa_x44 + frame_step)) ? 1u
                                                                                             : 0u;
    }
    return;
  }

  if (shield_held_inputs && batch->state.shield_hp[idx] > 0.0f) {
    const uint8_t entered_via_dash_91ad8 =
        (a0 == (uint16_t)MSL_ACT_DASH && batch->state.dash_x4[idx] != 0u &&
         batch->state.anim_frame_f32[idx] <= c->dash_iasa_x4c)
            ? 1u
            : 0u;
    enter_guard_on(batch, c, idx, guard_entry_via_wait_callback_from_current_row(batch, idx));
    batch->state.guard_entry_via_dash_91ad8[idx] = entered_via_dash_91ad8;
    if (dash_iasa_guard_admission_reaches_terminal_scalar(batch, c, idx, a0, a0_anim_frame)) {
      dash_iasa_apply_root_motion_exit_gr_vel_clamp(
          batch, msl_char_params_fast(batch->state.char_id[idx]), idx);
      dash_iasa_apply_terminal_velocity_scalar(batch, c, idx);
    }
    return;
  }
}

static inline void shieldbreak_update_anim_callback_pre_input(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              size_t idx) {
  if (batch == NULL || c == NULL) {
    return;
  }
  const uint16_t a0 = batch->state.action_id[idx];
  if (a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U && a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D &&
      a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_U &&
      a0 != (uint16_t)MSL_ACT_SHIELD_BREAK_STAND_D && a0 != (uint16_t)MSL_ACT_FURAFURA) {
    return;
  }
  if (batch->state.hitlag_started_frame[idx] != 0) {
    return;
  }
  if (a0 == (uint16_t)MSL_ACT_FURAFURA) {
    // Furafura_Anim keeps shield health pinned to x280, decrements fp->grab_timer, applies mash,
    // and exits to Wait through ft_8008A2BC when the timer expires.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_Furafura_Anim
    // refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    batch->state.shield_hp[idx] = c->shield_break_reset_health;
    float timer = batch->state.capture_grab_timer[idx];
    if (!(timer > 0.0f)) {
      timer = furafura_timer_init(batch, c, idx);
      const int16_t af = batch->state.action_frame[idx];
      if (af > 0) {
        timer -= (float)af * c->furafura_timer_decrement;
      }
    }
    timer -= c->furafura_timer_decrement;
    if (furafura_grab_mash_active(batch, c, idx)) {
      timer -= c->furafura_mash_decrement;
    }
    batch->state.capture_grab_timer[idx] = timer;
    if (timer <= 0.0f) {
      guard_enter_wait(batch, idx);
      batch->state.capture_grab_timer[idx] = 0.0f;
    }
    return;
  }

  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const float end = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
  if (!(end > 0.0f) || msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]) < end) {
    return;
  }

  if (a0 == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_U || a0 == (uint16_t)MSL_ACT_SHIELD_BREAK_DOWN_D) {
    shieldbreak_enter_stand(batch, idx, a0);
  } else {
    shieldbreak_enter_furafura(batch, c, idx);
  }
}

void action_update_anim_callback_pre_input_fighter(const MslFighterCallbackContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const MslCommonParams* c = msl_common_params();
  // Decomp ordering anchor:
  // - fighter Anim callbacks run in Fighter_8006A360 (prio 1) under !hitlag.
  // - input callback (IASA checks) runs later in Fighter_procUpdate (prio 3).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // Scope (current slice): only GuardReflect x14/x18 timer tick/expire is modeled here; this phase
  // must not consume current-frame input edges.
  const int bi = ctx->bi;
  const int p = ctx->p;
  const size_t idx = ctx->idx;
  {
    const uint32_t anim_u32 = batch->state.animation_index[idx];
    if (anim_u32 <= 0xFFFFu) {
      const uint16_t msid = (uint16_t)anim_u32;
      const uint16_t frame =
          msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
      uint8_t air_state = 0xFFu;
      if (move_tables_airborne_state_event_at_frame(batch->state.char_id[idx], msid, frame,
                                                    &air_state) != 0u) {
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        const uint8_t max_jumps = (ch != NULL) ? ch->max_jumps : batch->state.jumps_left[idx];
        // Movescript opcode 25 (ftAction_80071998) dispatch:
        // state=0 -> ftCommon_8007D7FC (air->ground common helper)
        // state=1 -> ftCommon_8007D5D4 (ground->air common helper)
        // state=2 -> ftCommon_8007D60C (ground->air alt helper)
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071998
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D5D4,ftCommon_8007D60C}
        if (air_state == 0u) {
          // Common air->ground helper ownership:
          // - ftAction_80071998 state=0 dispatches ftCommon_8007D7FC / ftCommon_8007D6A4.
          // - ftCommon_8007D6A4 sets fp->gr_vel = fp->self_vel.x and does not zero self_vel.x.
          // - grounded Fighter_procUpdate keeps fp->self_vel.x synchronized from fp->gr_vel.
          // refs/melee/src/melee/ft/ftaction.c::ftAction_80071998
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
          // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
          msl_ftcommon_8007d6a4(batch, ch, idx);
          if (ch == NULL) {
            batch->state.jumps_left[idx] = max_jumps;
          }
        } else if (air_state == 1u) {
          batch->state.on_ground[idx] = 0u;
          batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.jumps_left[idx] = (max_jumps > 0u) ? (uint8_t)(max_jumps - 1u) : 0u;
          msl_ftcommon_lock_ecb_8007d5d4(batch, idx);
        } else if (air_state == 2u) {
          batch->state.on_ground[idx] = 0u;
          batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
          batch->state.speed_ground_x_self[idx] = 0.0f;
          batch->state.jumps_left[idx] = 0u;
          msl_ftcommon_lock_ecb_8007d60c(batch, idx);
        }
      }
    }
  }
  rebound_update_anim_callback_pre_input(batch, idx);
  shieldbreak_update_anim_callback_pre_input(batch, c, idx);
  guard_update_grounded_anim_callback_pre_input(batch, idx);
  throw_flow_update_anim_callback_pre_input(batch, bi, p);
}

void action_update_anim_callbacks_pre_input_global(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  locomotion_update_anim_callbacks_pre_input(batch);
  blaster_update_anim_callbacks_pre_input(batch);
  grab_flow_update_anim_callbacks_pre_input(batch);
}

void action_update(MslBatch* batch) {
  const MslCommonParams* c = msl_common_params();
  if (batch != NULL) {
    const int num_players = (int)batch->config.num_players;
    for (int bi = 0; bi < batch->batch_size; bi++) {
      for (int p = 0; p < num_players; p++) {
        const size_t idx = msl_idx_player(bi, p);
        batch->state.guard_on_entered_this_frame[idx] = 0u;
        batch->state.guard_entry_via_dash_91ad8[idx] = 0u;
        batch->state.guard_jump_oos_entered_this_frame[idx] = 0u;
        batch->state.guard_reflect_entry_dash_terminal_scalar[idx] = 0u;
        batch->state.guard_reflect_entered_this_frame[idx] = 0u;
        if (batch->state.guard_on_entry_reflect_source_latch[idx] != 0u) {
          batch->state.guard_on_entry_reflect_source_latch[idx] =
              (uint8_t)(batch->state.guard_on_entry_reflect_source_latch[idx] - 1u);
        }
        batch->state.shine_jump_iasa_entered_this_frame[idx] = 0u;
      }
    }
  }
  // Grab/throw Anim-callback-shaped transitions should happen before the grounded locomotion IASA
  // chain (including shield entry). Example: Catch/CatchDash Anim end -> Wait (ft_8008A2BC) should
  // run before the next state's guard entry check (ftCo_80091A4C) so buffered shields can block
  // on the first actionable frame after a whiffed grab.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_CatchDash_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
  grab_flow_update_pre_physics(batch);
  // Run knockdown/damage Anim+IASA before generic locomotion so DamageFly->DamageFall transitions
  // can feed same-frame DamageFall IASA (e.g. ftCo_800CB870 jump check) in locomotion.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Anim,ftCo_DamageFlyRoll_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  knockdown_update_pre_physics(batch);
  locomotion_update_pre(batch);
  ledge_update_pre_physics(batch);
  // Keep Shine before Blaster so Down-B owns B-edge + down-stick entry; blaster resolver is
  // intentionally Neutral/Side/Up-only and relies on this ordering.
  shine_update_pre_physics(batch);
  blaster_update_pre_physics(batch);
  marth_specials_update_pre_physics(batch);
  sheik_specials_update_pre_physics(batch);
  falcon_specials_update_pre_physics(batch);
  // Shield recharge is owned by Fighter_ProcessHit_8006D1EC under the `!fp->x221A_b7` gate, not
  // by locomotion. Run it after the frame's state-entry callbacks so the gate observes the current
  // state (for example SpecialLwStart after a shine entry), and do not suppress it during hitlag.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  action_update_shield_recharge_post_state(batch, c);
}
