#include "state_flags.h"
#include "ids.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "batch_internal.h"
#include "common_params.h"
#include "char_params.h"
#include "guard_lifecycle.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "stage_collision.h"

static inline uint8_t state_flags_2218_allow_interrupt_attackair_action(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint8_t state_flags_is_damage_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_is_damage_fly_action(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t state_flags_is_down_damage_action(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D)
             ? 1u
             : 0u;
}

static inline uint8_t state_flags_221a_b5_capture_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_CAPTURE_PULLED_HI:
    case MSL_ACT_CAPTURE_WAIT_HI:
    case MSL_ACT_CAPTURE_PULLED_LW:
    case MSL_ACT_CAPTURE_WAIT_LW:
    case MSL_ACT_CAPTURE_CUT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_2218_allow_interrupt_grounded_attack_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_11:
    case MSL_ACT_ATTACK_DASH:
    case MSL_ACT_ATTACK_S3_HI:
    case MSL_ACT_ATTACK_S3_HI_S:
    case MSL_ACT_ATTACK_S3_S:
    case MSL_ACT_ATTACK_S3_LW_S:
    case MSL_ACT_ATTACK_S3_LW:
    case MSL_ACT_ATTACK_HI3:
    case MSL_ACT_ATTACK_LW3:
    case MSL_ACT_ATTACK_S4_HI:
    case MSL_ACT_ATTACK_S4_HI_S:
    case MSL_ACT_ATTACK_S4_S:
    case MSL_ACT_ATTACK_S4_LW_S:
    case MSL_ACT_ATTACK_S4_LW:
    case MSL_ACT_ATTACK_HI4:
    case MSL_ACT_ATTACK_LW4:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_2218_attack12_allow_interrupt_action(uint16_t action_id) {
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_ATTACK_12);
}

static inline uint8_t state_flags_action_allow_interrupt_at_frame(uint8_t char_id,
                                                                  uint16_t action_id, float frame) {
  if (state_flags_2218_allow_interrupt_attackair_action(action_id)) {
    return move_tables_attackair_allow_interrupt(char_id, action_id, frame);
  }
  if (state_flags_2218_allow_interrupt_grounded_attack_action(action_id) ||
      state_flags_2218_attack12_allow_interrupt_action(action_id)) {
    return move_tables_grounded_attack_allow_interrupt(char_id, action_id, frame);
  }
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_N || action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return move_tables_escape_allow_interrupt(char_id, action_id, frame);
  }
  return 0u;
}

static inline uint8_t state_flags_2218_action_owns_reflecting(const MslBatch* batch, size_t idx,
                                                              uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_LW_HIT:
    case MSL_ACT_FX_SPECIAL_LW_TURN:
    case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_LW_HIT:
    case MSL_ACT_FX_SPECIAL_AIR_LW_TURN:
      // Fox/Falco reflector Loop/Hit/Turn states create ReflectDesc and set fp->reflecting.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
      //   ftFx_SpecialLwLoop_Enter,ftFx_SpecialLwHit_Enter,ftFx_SpecialLwTurn_Enter,
      //   ftFx_SpecialAirLwLoop_Enter,ftFx_SpecialAirLwHit_Enter,ftFx_SpecialAirLwTurn_Enter}
      return 1u;
    case MSL_ACT_GUARD_REFLECT:
      // GuardReflect keeps the reflecting lane only while mv.co.guard.x14 is active.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_8009370C,ftCo_80093BC0}
      return (batch != NULL && batch->state.guard_reflect_timer_x14[idx] != 0u) ? 1u : 0u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_guard_setoff_hitlag_handoff_phase(const MslBatch* batch,
                                                                    size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  return batch->state.guard_setoff_hitlag_exit_phase_u8[idx];
}

static inline uint8_t state_flags_guard_setoff_post_hitlag_owner(const MslBatch* batch,
                                                                 size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  return batch->state.guard_setoff_post_hitlag_owner_u8[idx];
}

static inline uint8_t state_flags_221f_dead_start_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DEAD_DOWN:
    case MSL_ACT_DEAD_LEFT:
    case MSL_ACT_DEAD_RIGHT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_221f_dead_up_fall_hitcamera_action(uint16_t action_id) {
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_ICE);
}

static inline uint8_t state_flags_match_flow_respawn_action(uint16_t action_id) {
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_REBIRTH ||
                   action_id == (uint16_t)MSL_ACT_REBIRTH_WAIT);
}

static inline uint8_t state_flags_camera_target_live_pose_action(uint16_t action_id) {
  return (uint8_t)(state_flags_221f_dead_start_action(action_id) != 0u ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_STAR ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA ||
                   action_id == (uint16_t)MSL_ACT_DEAD_UP_FALL_HIT_CAMERA_FLAT ||
                   state_flags_match_flow_respawn_action(action_id) != 0u);
}

static inline uint8_t state_flags_camera_overlap_stage_cam_bounds(const MslBatch* batch, size_t idx,
                                                                  float tolerance) {
  if (batch == NULL) {
    return 0u;
  }
  MslStageBounds cam = {0};
  if (!stage_collision_get_cam_bounds_world(batch->state.stage_id[idx / MSL_MAX_PLAYERS], &cam)) {
    return 0u;
  }
  const float x = batch->state.camera_target_world_x_f32[idx];
  const float y = batch->state.camera_target_world_y_f32[idx];
  const float r = batch->state.camera_box_radius_f32[idx] + tolerance;
  return (uint8_t)(x >= (cam.left - r) && x < (cam.right + r) && y >= (cam.bottom - r) &&
                   y < (cam.top + r));
}

static inline uint8_t state_flags_camera_below_stage_cam_bounds(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  MslStageBounds cam = {0};
  if (!stage_collision_get_cam_bounds_world(batch->state.stage_id[idx / MSL_MAX_PLAYERS], &cam)) {
    return 0u;
  }
  return (uint8_t)(batch->state.camera_target_world_y_f32[idx] < cam.bottom);
}

static inline void state_flags_refresh_camera_target_from_pose(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch == NULL || ch->camera_box_radius <= 0.0f) {
    return;
  }
  const uint32_t msid_u32 = batch->state.animation_index[idx];
  if (msid_u32 > 0xFFFFu) {
    return;
  }
  float m[12];
  const uint16_t msid = (uint16_t)msid_u32;
  const uint16_t frame = msl_anim_frame_floor_u16(batch->state.anim_frame_f32[idx]);
  if (anim_pose_get_matrix(batch->state.char_id[idx], msid, frame,
                           ch->camera_zoom_target_bone_part_id, m) != 0) {
    return;
  }

  const float off_x = ch->camera_zoom_target_offset_x;
  const float off_y = ch->camera_zoom_target_offset_y;
  const float off_z = ch->camera_zoom_target_offset_z;
  float lx = m[0] * off_x + m[1] * off_y + m[2] * off_z + m[3];
  float ly = m[4] * off_x + m[5] * off_y + m[6] * off_z + m[7];
  float lz = m[8] * off_x + m[9] * off_y + m[10] * off_z + m[11];
  float scale = batch->state.fighter_scale_y[idx];
  if (!(scale > 0.0f)) {
    scale = 1.0f;
  }
  const float model_scaling = (ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  scale *= model_scaling;
  lx *= scale;
  ly *= scale;
  lz *= scale;
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

  // Runtime camera target ownership:
  // - ftCamera_UpdateCameraBox refreshes `camera_box->x1C` from the camera target bone each frame,
  // - ftLib_800866DC applies the live joint matrix plus root facing rotation,
  // - ftLib_80086A8C consumes that subject point for the x221F_b0 visibility branch.
  // refs/melee/src/melee/ft/ftcamera.c::ftCamera_UpdateCameraBox
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800866DC,ftLib_80086A8C}
  // data/characters/{fox,falco}.json: camera_zoom_target_bone_part_id,
  //   camera_zoom_target_offset, camera_box_radius, model_scaling
  // data/anims/{fox,falco}.bin: SSANIM01 pose matrices
  batch->state.camera_target_world_x_f32[idx] = batch->state.pos_x[idx] + facing_dir * lz;
  batch->state.camera_target_world_y_f32[idx] = batch->state.pos_y[idx] + ly;
  batch->state.camera_target_world_z_f32[idx] = batch->state.pos_z[idx] - facing_dir * lx;
  batch->state.camera_box_radius_f32[idx] =
      ch->camera_box_radius * batch->state.fighter_scale_y[idx];
}

void state_flags_refresh_camera_targets_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // ftCamera_UpdateCameraBox is a live fighter-camera subject refresh, not a seed-only
      // replay surface. Run it before physics so x221F visibility checks later in the frame do not
      // compare against stale teacher-forced camera points from rollout start.
      // refs/melee/src/melee/ft/ftcamera.c::ftCamera_UpdateCameraBox
      // refs/melee/src/melee/ft/ftlib.c::{ftLib_800866DC,ftLib_80086A8C}
      state_flags_refresh_camera_target_from_pose(batch, idx);
    }
  }
}

static void state_flags_refresh_post_frame_impl(MslBatch* batch, const uint8_t* mask_bytes,
                                                size_t mask_stride_bytes) {
  if (batch == NULL) {
    return;
  }

  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

  // fp+0x221A:
  // - 0x01 = fp->x221A_b7
  // - 0x08 = isFastFalling
  // - 0x20 = isHitlag
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221A bitfields)
  //
  // Decomp: fp->x221A_b7 is toggled alongside shield activation:
  // - set on GuardOn entry after ftColl_8007B1B8 (ftCo_80092450),
  // - cleared on GuardReflect entry (ftCo_8009388C) and shield break (ftCo_800925A4).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_8009388C,ftCo_800925A4}

  // fp+0x221B:
  // - 0x80 = isShieldActive (fp->x221B_b0 in the decomp bitfield layout)
  // - 0x04 = fp->x221B_b5 (grab-owner latch; set while this fighter owns a grabbed victim)
  // Bit-order note: fp+0x221B b* numbering is MSB-first in GALE01/Slippi packing
  // (b0==0x80 ... b5==0x04), matching refs/melee/src/melee/ft/types.h + SendGamePostFrame.asm.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221B bitfields)

  // fp+0x221C:
  // - 0x80 = x221C_b0 (damage no-reaction lane)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfields)
  // - 0x02 = isHitstun
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfields)
  // Movescript opcode 52 writes fp->x221C_u16_y (3-bit field). Slippi emits only fp+0x221C
  // (the high byte at +0x221C), and the overlapping exported lane there is high-byte bit0
  // (mask 0x01).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80072C6C
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A1B8
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield layout)

  // fp+0x221C GuardReflect flags:
  // - x221C_b1 (mask 0x40) is cleared when mv.co.guard.x14 expires,
  // - x221C_b2 (mask 0x20) is "Powershield Active Bool" (Slippi post-frame) and is cleared when
  //   mv.co.guard.x18 expires,
  // - x221C_b3 (mask 0x10) is a 1-frame entry flag cleared on the next Anim tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (0x221C 0x20 = Powershield Active Bool)
  // Bitfield layout: refs/melee/src/melee/ft/types.h (fp+0x221C bits 0..3 map to masks 0x80..0x10).

  // Seed/state timer representation uses a +1 bias; derive the entry value from ftCommonData.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c (mv.co.guard.x14 = p_ftCommonData->x2A4)
  const MslCommonParams* c = msl_common_params();
  uint8_t guard_reflect_timer_x14_init = 0;
  uint8_t guard_x10_init = 0;
  if (c != NULL) {
    guard_reflect_timer_x14_init = msl_guard_reflect_timer_x14_init(c);
    guard_x10_init = msl_guard_x10_raw_init_u8(c);
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (mask_bytes != NULL && mask_bytes[(size_t)bi * mask_stride_bytes] == 0u) {
      continue;
    }
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
          batch->state.action_id[idx] == (uint16_t)MSL_ACT_DEAD_DOWN) {
        // Eliminated player slots have no live Fighter owner; Slippi exposes the inactive slot with
        // zeroed state flag bytes. Team stock-share can later create a fresh Rebirth owner, but
        // until then stale seeded flag bits must not survive on the terminal dead slot.
        // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918_inline
        for (size_t k = 0; k < (size_t)MSL_STATE_FLAGS_BYTES; k++) {
          batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES + k] = 0u;
        }
        continue;
      }
      const uint16_t action_id = batch->state.action_id[idx];
      const uint16_t prev_action = batch->state.prev_action_id[idx];

      // fp+0x2218 bit0 (mask 0x80): allow_interrupt.
      // - Action command opcode handler `ftAction_80071950` sets fp->allow_interrupt = true.
      // - Attack* entries clear fp->allow_interrupt = false, then IASA gates on this bit.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c
      //
      // Seed-bridge snapshot parity:
      // - Slippi's post-frame byte can reflect the prior-frame allow_interrupt lane relative to our
      //   one-step reseed snapshot ordering (Anim tick + callback side effects).
      // - This probe is snapshot parity glue for evaluation; it is not live gameplay logic.
      // TODO: replace this bridge with an explicit seed lane for fp+0x2218 bit0 (x2218_b0) once
      // causal derivation/seeding is available.
      const size_t flags_2218_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      uint8_t f2218 = batch->state.state_flags[flags_2218_i];
      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      float allow_interrupt_anim_probe = anim_frame_f32;
      const uint32_t allow_interrupt_anim_u32 = batch->state.animation_index[idx];
      const int16_t action_frame_i = batch->state.action_frame[idx];
      // Restrict previous-frame probe to true no-submotion negative-lane snapshots.
      // For normal in-motion rows, sample current anim frame so allow_interrupt windows are not
      // shifted one frame late.
      if (allow_interrupt_anim_u32 == 0xFFFFFFFFu && action_frame_i < 0 && anim_frame_f32 >= 1.0f) {
        allow_interrupt_anim_probe = anim_frame_f32 - 1.0f;
      }
      uint8_t allow_interrupt_known = 0u;
      uint8_t allow_interrupt = 0u;
      if (state_flags_2218_allow_interrupt_attackair_action(action_id)) {
        allow_interrupt_known = 1u;
        allow_interrupt = move_tables_attackair_allow_interrupt(
            batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
      } else if (state_flags_2218_allow_interrupt_grounded_attack_action(action_id)) {
        allow_interrupt_known = 1u;
        allow_interrupt = move_tables_grounded_attack_allow_interrupt(
            batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
      } else if (state_flags_2218_attack12_allow_interrupt_action(action_id)) {
        // Attack12 command ownership for fp+0x2218 bit0:
        // - Attack12_Anim is script driven and Attack12_IASA can continue through the same
        //   allow_interrupt lane as Attack11/S3/etc.
        // - Keep this lane set-only until full Attack12 command-bit seeding is modeled; clearing
        //   from a replay-seeded Attack12 snapshot loses unrelated x2218_b1/b2 history.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{
        //   ftCo_Attack12_Anim,ftCo_Attack12_IASA,checkAttack13}
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        // data/moves/{fox,falco}.json moves["ftCo_SM_Attack12"]["events"]
        if (move_tables_grounded_attack_allow_interrupt(batch->state.char_id[idx], action_id,
                                                        allow_interrupt_anim_probe)) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        }
      } else if (action_id == (uint16_t)MSL_ACT_ESCAPE_N ||
                 action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
        // EscapeN/EscapeAir `allow_interrupt` lane:
        // - ftAction command script emits `allow_interrupt` (ftAction_80071950) during EscapeN and
        //   EscapeAir.
        // - Slippi state_flags[0] bit 0x80 mirrors fp->allow_interrupt (fp+0x2218 bit0).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // data/moves/{fox,falco}.json moves["ftCo_SM_Escape{N,Air}"]["events"]
        // Escape script timing is integer-framed in-suite; use action_frame to avoid float-timebase
        // probe jitter on this lane.
        //
        // Keep this as a one-way command write (set-only). EscapeN can inherit a pre-existing
        // allow_interrupt carry on entry snapshots; do not force-clear that carry when the script
        // event is inactive.
        if (move_tables_escape_allow_interrupt(batch->state.char_id[idx], action_id,
                                               (float)batch->state.action_frame[idx])) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        }
      }
      if (allow_interrupt_known) {
        if (allow_interrupt) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        }
      }
      const uint16_t prev_action_2218 = batch->state.prev_action_id[idx];
      // Jab command ownership (fp+0x2218 x2218_b1/x2218_b2):
      // - ftAction_80071AE8 sets x2218_b1 from set_jab_combo script commands.
      // - ftAction_80071B28 sets x2218_b2 from set_jab_rapid script commands.
      // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071AE8,ftAction_80071B28}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
      // data/moves/{fox,falco}.json moves["ftCo_SM_Attack11"]["events"] set_jab_combo
      // data/moves/{fox,falco}.json moves["ftCo_SM_Attack12"]["events"] set_jab_rapid
      //
      // Keep this lane narrow to Attack11/Attack12 where command windows are extracted.
      if (action_id == (uint16_t)MSL_ACT_ATTACK_11 || action_id == (uint16_t)MSL_ACT_ATTACK_12) {
        const uint8_t jab_combo_active = move_tables_jab_combo_active(
            batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
        const uint8_t jab_rapid_active = move_tables_jab_rapid_active(
            batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
        if (jab_combo_active) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_B1;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_B1;
        }
        if (jab_rapid_active) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_B2;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_B2;
        }
      }
      const uint8_t shine_start_platform_pass_reflecting =
          ((action_id == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) &&
           (f2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) != 0u)
              ? 1u
              : 0u;
      if (action_id != prev_action_2218 &&
          state_flags_2218_action_owns_reflecting(batch, idx, action_id) == 0u &&
          shine_start_platform_pass_reflecting == 0u) {
        // Fighter_ChangeMotionState clears fp->reflecting on motion changes before the destination
        // callback can recreate a new reflector. Preserve this as a motion-change reset rather
        // than seed-carrying a stale Shine/GuardReflect descriptor into unrelated destinations
        // such as Damage*.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState (fp->reflecting = false)
        // Exception: ftFx_SpecialLwStart_Pass recreates ReflectDesc after the motion-state change
        // to SpecialAirLwStart, so a current-frame bit already set by that pass remains live.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Pass
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 -> state_flags[0])
        f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
      }
      if (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        // GuardReflect reflecting-owner lane:
        // - ftColl_CreateReflectHit sets fp->reflecting = true on GuardReflect admission.
        // - ftCo_80093BC0 clears fp->reflecting when the x14 reflect window expires.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
        if (batch->state.guard_reflect_timer_x14[idx] != 0u) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
        }
      } else if (state_flags_2218_action_owns_reflecting(batch, idx, action_id) != 0u) {
        // Fox/Falco SpecialLw collision callbacks can change Ground<->Air Loop/Hit/Turn after
        // reflector_bubbles_refresh() has already published the current reflector descriptor.
        // The destination callback immediately recreates or preserves fp->reflecting, so the
        // post-frame raw fp+0x2218 byte must publish bit0x10 for those current-frame destinations.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialLwLoop_GroundToAir,ftFx_SpecialAirLwLoop_AirToGround,
        //   ftFx_SpecialLwHit_GroundToAir,ftFx_SpecialAirLwHit_AirToGround,
        //   ftFx_SpecialLwTurn_GroundToAir,ftFx_SpecialAirLwTurn_GroundToAir}
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 -> state_flags[0])
        f2218 |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
      } else if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
                 prev_action_2218 == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        // GuardReflect -> GuardSetOff handoff:
        // - shieldstun entry itself still uses Fighter_ChangeMotionState reset ownership, and
        // - the destination GuardSetOff row no longer owns the live `reflecting` lane from the
        //   GuardReflect descriptor.
        // Keep this restricted to true GuardReflect exits; replay-real locomotion-driven
        // GuardSetOff admission rows can still carry the seeded `reflecting` bit.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
        f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
      }
      const uint8_t modeled_damage_entry_flag_owner =
          (state_flags_is_damage_action(action_id) && action_id != prev_action &&
           batch->state.prev_action_frame[idx] >= 0 &&
           ((prev_action == (uint16_t)MSL_ACT_SQUAT && action_id == (uint16_t)MSL_ACT_DAMAGE_N_2) ||
            (prev_action == (uint16_t)MSL_ACT_ATTACK_LW3 &&
             action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_N) ||
            (prev_action == (uint16_t)MSL_ACT_ESCAPE_N &&
             action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_N)))
              ? 1u
              : 0u;
      if (modeled_damage_entry_flag_owner) {
        // ProcessHit/Damage entry does not synthesize allow_interrupt. For these bounded source
        // owners, the post-frame fp+0x2218 bit0 mirrors the interrupted source action's
        // command-script authority; otherwise stale seeded allow clears. Broader Damage-entry flag
        // inheritance needs hidden source phase that is not modeled for every DamageFly/DamageFall
        // family yet.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B62C
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
        const float source_frame = (float)(batch->state.prev_action_frame[idx] + 1);
        if (state_flags_action_allow_interrupt_at_frame(batch->state.char_id[idx], prev_action,
                                                        source_frame)) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
        }
      }
      if (state_flags_is_damage_action(action_id) && action_id != prev_action &&
          batch->state.prev_action_frame[idx] >= 0 &&
          (prev_action == (uint16_t)MSL_ACT_ATTACK_11 ||
           prev_action == (uint16_t)MSL_ACT_ATTACK_12)) {
        // ProcessHit can interrupt Attack11/12 after the current source frame's command script has
        // toggled x2218_b1. Damage entry itself does not clear that jab-combo bit, so the
        // destination Damage* row carries the source script result rather than the stale seed byte.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{ftCo_Attack11_IASA,checkAttack12}
        // data/moves/{fox,falco}.json moves["ftCo_SM_Attack11"]["events"] set_jab_combo
        const float source_frame = (float)(batch->state.prev_action_frame[idx] + 1);
        if (move_tables_jab_combo_active(batch->state.char_id[idx], prev_action, source_frame)) {
          f2218 |= (uint8_t)MSL_STATE_FLAG_2218_B1;
        } else {
          f2218 &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_B1;
        }
      }
      batch->state.state_flags[flags_2218_i] = f2218;

      // 0x221A: HasIntangOrInvinc + isFastFalling.
      const size_t flags_221a_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
      uint8_t f221a = batch->state.state_flags[flags_221a_i];
      if (batch->state.fall_fast[idx] != 0) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_IS_FASTFALL;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_FASTFALL;
      }
      // Slippi packs fp+0x221A bit0x20 as isHitlag; keep this byte causally owned by the runtime
      // hitlag counter to avoid stale seeded carryover.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (fp->hitlag_remaining_frames update path)
      if (batch->state.hitlag[idx] > 0) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
        // Decomp: Fighter_8006A1BC clears fp->x221A_b3 when hitlag reaches 0.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B3;
      }

      // x221A_b5 ownership:
      // - ftColl_8007B0C0 sets/clears fp->x221A_b5 based on whole-capsule hit status argument.
      // - ftColl_8007B128 sets fp->x221A_b5 when any part hurt capsule state is non-enabled.
      // Mirror that by marking b5 when any current hurtcap in the active set is non-enabled.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B0C0,ftColl_8007B128}
      uint8_t any_non_enabled_hurtcap = 0u;
      const uint8_t cap_count = batch->state.hurtcap_count[idx];
      const size_t cap_base = idx * (size_t)MSL_MAX_HURTCAPS;
      for (uint8_t ci = 0; ci < cap_count && ci < (uint8_t)MSL_MAX_HURTCAPS; ci++) {
        if (batch->state.hurtcap_enabled[cap_base + (size_t)ci] == 0u) {
          any_non_enabled_hurtcap = 1u;
          break;
        }
      }
      if (any_non_enabled_hurtcap) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_B5;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B5;
      }
      if (state_flags_221a_b5_capture_action(action_id) && action_id != prev_action) {
        // Capture destination entries clear stale x221A_b5 carry:
        // - CapturePulled/Wait/Cut motion states use `ftCo_MF_Capture`,
        // - that motion-flag set does not include Ft_MF_KeepColAnimHitStatus, so generic
        //   Fighter_ChangeMotionState reset owns colanim/hurt-status clear on the destination row.
        // refs/melee/src/melee/ft/chara/ftCommon/forward.h::{ftCo_MF_CatchWait,ftCo_MF_Capture}
        // refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_CapturePulledHi,ftCo_MS_CaptureWaitHi,
        //   ftCo_MS_CapturePulledLw,ftCo_MS_CaptureWaitLw,ftCo_MS_CaptureCut}
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B5;
      }
      if (state_flags_is_damage_action(action_id) && action_id != prev_action &&
          batch->state.action_frame[idx] == 1 && batch->state.hitlag[idx] > 0u &&
          batch->state.hitstun[idx] > 0u) {
        // Fresh Damage* destination entry also clears x221A_b5:
        // - motion-state reset on Fighter_ChangeMotionState restores normal hurt-capsule status on
        //   the destination before the new damage hitlag window is observed,
        // - so stale whole-capsule disable carries should not survive onto the first replay-visible
        //   Damage* destination row with live hitlag/hitstun.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B0C0,ftColl_8007B128}
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B5;
        // Fresh Damage* destination entry sets x221A_b3 while hitlag is active:
        // - Fighter_ProcessHit enables SDI / x221A_b3 on the knockback-owning hitlag start path,
        // - Fighter_8006A1BC clears x221A_b3 when hitlag ends,
        // - so the first replay-visible Damage* destination rows with live hitlag/hitstun should
        //   expose the bit during the new hitlag window.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_B3;
      }
      if (state_flags_match_flow_respawn_action(action_id)) {
        // Rebirth/RebirthWait set dedicated match-flow visibility/intangibility bits, but they do
        // not own the whole-capsule x221A_b5 lane through the generic hurtcap status path.
        // Decomp writes x221E_b2, x221E_b1, x221D_b5 on Rebirth/RebirthWait entry; x221A_b5 is not
        // part of the respawn platform setup.
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x221A -> state_flags[1])
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B5;
      }

      // x221B_b5 ownership (grab-owner latch):
      // - set in catch collision when this fighter acquires victim_gobj (ftGrabDist),
      // - cleared on throw release helper and capture-cut paths.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftGrabDist}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c
      //
      // Internal mapping: `grab_owner_port` is this sim's explicit victim_gobj owner link; derive
      // x221B_b5 from "any live grabbed victim points at owner p" to avoid stale seeded carryover.
      const size_t flags_221b_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      uint8_t f221b = batch->state.state_flags[flags_221b_i];
      uint8_t owner_has_grabbed_victim = 0;
      for (int v = 0; v < num_players; v++) {
        if (v == p) {
          continue;
        }
        const size_t vidx = msl_idx_player(bi, v);
        if (batch->state.stocks[vidx] == 0) {
          continue;
        }
        if (batch->state.grab_owner_port[vidx] != (uint8_t)p) {
          continue;
        }
        if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
          continue;
        }
        owner_has_grabbed_victim = 1;
        break;
      }
      if (owner_has_grabbed_victim) {
        f221b |= (uint8_t)MSL_STATE_FLAG_221B_B5;
      } else {
        f221b &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B5;
      }
      // Captured-victim motions do not own shield-desc lifecycle; the generic motion-state reset
      // clears fp->x221B_b0 on these transitions.
      // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState reset clears fp->x221B_b0)
      // refs/melee/src/melee/ft/chara/ftCommon/forward.h (CapturePulled*/CaptureWait*/CaptureDamage*)
      if (msl_action_is_grabbed_victim(action_id)) {
        f221b &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
      }
      // Approximate fp->x221A_b7 from shield activation (fp->x221B_b0), but only for the
      // guard-family states that own shield descriptor lifecycle in decomp.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_8009388C,ftCo_800925A4}
      const uint8_t b7_shield_active =
          ((f221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u &&
           msl_guard_lifecycle_action_uses_guard_shield(batch->state.action_id[idx]))
              ? 1u
              : 0u;
      if (b7_shield_active) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_B7;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B7;
      }
      batch->state.state_flags[flags_221b_i] = f221b;
      batch->state.state_flags[flags_221a_i] = f221a;

      // 0x221C: isHitstun derived from hitstun frames left.
      const size_t flags_221c_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      uint8_t f221c = batch->state.state_flags[flags_221c_i];
      const uint8_t down_damage_hitstun_flag_carry =
          (state_flags_is_down_damage_action(batch->state.action_id[idx]) &&
           (f221c & (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN) != 0u)
              ? 1u
              : 0u;
      if (batch->state.hitstun[idx] > 0 || down_damage_hitstun_flag_carry) {
        f221c |= (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
      } else {
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
      }

      // Script-owned x221C_u16_y contribution to fp+0x221C high-byte bit0:
      // - opcode 52 writes a 3-bit payload (dbanim "L/R/T", masks 1/2/4),
      // - x221C_u16_y occupies bits 7..9; in this MSB-first packed lane, the "T" bit (0x4) is
      //   the overlap that lands in the recorded fp+0x221C byte as mask 0x01.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80072C6C
      // refs/melee/src/melee/db/dbanim.c
      // refs/melee/src/melee/ft/types.h (fp+0x221C_u16_y : 3 at bits 7..9)
      const uint8_t seed_x221c_y_visible =
          (f221c & (uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE) != 0u ? 1u : 0u;
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 <= 0xFFFFu) {
        const uint16_t msid = (uint16_t)anim_u32;
        const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
        uint8_t y_flags = 0u;
        if (move_tables_state_flags_221c_y_at_frame(batch->state.char_id[idx], msid, frame,
                                                    &y_flags) != 0u) {
          if (y_flags & 0x4u) {
            f221c |= (uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
          } else {
            f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
          }
        } else {
          // No opcode-52 timeline for this (char, msid): keep x221C_u16_y visible bit cleared to
          // avoid stale seeded carryover into states that do not script-drive this lane.
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
        }
      } else {
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
      }
      if (action_id == (uint16_t)MSL_ACT_WALK_SLOW ||
          action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL) {
        // x221C_u16_y reset ownership on grounded locomotion/catch-pull transitions:
        // - Fighter_ChangeMotionState clears fp->x221C_u16_y when Ft_MF_Unk24 is not set.
        // - WalkSlow entry runs with Ft_MF_None (no keep flag), so carry-in opcode-52 state from
        //   previous motions should be reset on this destination.
        // - Catch/CatchDash entry path is also ChangeMotionState(..., flags=0); keep CatchDashPull
        //   aligned with that grounded catch-flow reset ownership in one-step parity.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
      }
      if ((action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
           prev_action == (uint16_t)MSL_ACT_FX_SPECIAL_S && batch->state.action_frame[idx] > 0) ||
          (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S &&
           prev_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S && seed_x221c_y_visible == 0u &&
           batch->state.action_frame[idx] > 0)) {
        // Fox/Falco side-special ground->air transition:
        // - ftFx_SpecialS_GroundToAir calls Fighter_ChangeMotionState with
        //   FTFOX_SPECIALS_COLL_FLAG and the current animation frame,
        // - that flag set does not include Ft_MF_Unk24, so Fighter_ChangeMotionState clears
        //   fp->x221C_u16_y,
        // - because the destination starts past frame 0, SpecialAirS's frame-0 opcode-52 command
        //   is not replayed. Later same-action rows preserve the hidden clear.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::FTFOX_SPECIALS_COLL_FLAG
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_GroundToAir
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IN_DAMAGE;
      }

      // GuardReflect flag parity (x221C_b1 / x221C_b2 / x221C_b3) driven by GuardReflect timers.
      if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
        const uint8_t t14 = batch->state.guard_reflect_timer_x14[idx];
        const uint8_t t18 = batch->state.guard_reflect_timer_x18[idx];
        // x221C_b1 remains set until the reflect window expires (t14==0 under +1 bias).
        if (t14 != 0) {
          f221c |= (uint8_t)MSL_STATE_FLAG_221C_B1;
        } else {
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        // x221C_b2 ("Powershield Active Bool") remains set until the powershield timer expires
        // (t18==0 under +1 bias).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        if (t18 != 0) {
          f221c |= (uint8_t)MSL_STATE_FLAG_221C_B2;
        } else {
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        // x221C_b3 is a 1-frame entry flag; it is set on GuardReflect entry and cleared on the
        // next Anim tick (ftCo_80093BC0).
        if (t14 == guard_reflect_timer_x14_init && guard_reflect_timer_x14_init != 0) {
          f221c |= (uint8_t)MSL_STATE_FLAG_221C_B3;
        } else {
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;
        }
      } else if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
                 prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF &&
                 prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT) {
        const uint8_t t14 = batch->state.guard_reflect_timer_x14[idx];
        const uint8_t t18 = batch->state.guard_reflect_timer_x18[idx];
        // Locomotion/grounded shield-admission -> GuardSetOff destination frame:
        // - shield contact can route straight into GuardSetOff without exposing an intermediate
        //   GuardReflect post-frame,
        // - powershield-desc setup (ftCo_80093A50) still writes the GuardReflect timers/flags, and
        // - the destination GuardSetOff post-frame observes those active x221C_b1/x221C_b2 bits.
        // Keep this narrow to non-GuardReflect entries so stale-x18 GuardReflect->GuardSetOff
        // carry lanes remain owned by their existing callback/timer bridges.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_GuardSetOff_Anim}
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
        if (t14 != 0) {
          f221c |= (uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        if (t18 != 0) {
          f221c |= (uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;
      } else {
        // Decomp: x221C_b3 is the 1-frame GuardReflect-entry latch written on ftCo_8009388C entry
        // and cleared by the next ftCo_80093BC0 callback pass; non-GuardReflect states do not own
        // this bit, so clear it outside GuardReflect.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093BC0}
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B3;

        // Decomp guard hold ownership: the reflect-window bit x221C_b1 is tied to GuardReflect
        // timer/callback ownership and should not persist through GuardOn/Guard hold windows.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_Guard_Anim,ftCo_80093BC0}
        if (action_id == (uint16_t)MSL_ACT_GUARD_ON || action_id == (uint16_t)MSL_ACT_GUARD) {
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }

        if (prev_action == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            action_id != (uint16_t)MSL_ACT_GUARD_REFLECT &&
            action_id != (uint16_t)MSL_ACT_GUARD_SET_OFF) {
          // GuardReflect exit timer-bit ownership:
          // - Fighter_procUpdate runs GuardReflect_Anim before GuardReflect_IASA.
          // - GuardReflect_Anim calls ftCo_80093BC0, which clears x221C_b3 immediately and keeps
          //   x221C_b1/x221C_b2 only while mv.co.guard.x14/x18 remain live.
          // - IASA exits such as KneeBend (ftCo_800CB024) and Pass (ftCo_8009A080) then change
          //   motion state, but the post-frame still exposes the timer-bit state produced by that
          //   same callback tick rather than a destination-action hardcoded mask.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA}
          const uint8_t t14_after_anim = msl_guard_reflect_timer_after_anim_tick(
              batch->state.guard_reflect_timer_x14_seed[idx],
              batch->state.hitlag_started_frame[idx]);
          const uint8_t t18_after_anim = msl_guard_reflect_timer_after_anim_tick(
              batch->state.guard_reflect_timer_x18_seed[idx],
              batch->state.hitlag_started_frame[idx]);
          if (t14_after_anim != 0u) {
            f221c |= (uint8_t)MSL_STATE_FLAG_221C_B1;
          } else {
            f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
          }
          if (t18_after_anim != 0u) {
            f221c |= (uint8_t)MSL_STATE_FLAG_221C_B2;
          } else {
            f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
          }
        }

        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            batch->state.action_frame[idx] == 0 && batch->state.hitlag[idx] > 0u &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] > 0u &&
            f221c == (uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2)) {
          // GuardReflect -> GuardSetOff active-timer handoff:
          // - the destination GuardSetOff row still observes hitlag, so the powershield-active x18
          //   lane can remain visible,
          // - but ftCo_80093BC0 ties x221C_b1 to x14, and once the reflect-window timer has
          //   expired on the destination row, that bit no longer has a live owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF && batch->state.hitlag[idx] == 0u &&
            state_flags_guard_setoff_hitlag_handoff_phase(batch, idx) == 3u &&
            state_flags_guard_setoff_post_hitlag_owner(batch, idx) == 2u &&
            batch->state.prev_action_frame[idx] > 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            (f221c & (uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2)) ==
                (uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2)) {
          // GuardSetOff stale reflect-window carry on the first steady post-hitlag row:
          // - the current seeded GuardSetOff snapshot has already advanced off the frozen af0 lane,
          // - GuardSetOff_Anim/ftCo_80093BC0 owns the steady row, and
          // - once x14 is expired, ftCo_80093BC0 no longer owns x221C_b1 while x221C_b2 can
          //   persist independently under its x18 lifetime.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] > 0u &&
            f221c == (uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2)) {
          // GuardSetOff hitlag-exit active-timer split:
          // - Fighter_8006A1BC decrements hitlag before Fighter_8006A360 runs GuardSetOff_Anim.
          // - GuardSetOff_Anim calls ftCo_80093BC0; that helper decrements/clears x221C_b1 from
          //   mv.co.guard.x14 independently from the longer x18/x221C_b2 powershield-active lane.
          // - Therefore a hitlag-exit GuardSetOff row can publish only x221C_b2 while x18 remains
          //   live.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        const uint8_t guardsetoff_first_steady_x10_phase =
            (batch->state.guard_x10[idx] == 7u ||
             (batch->state.guard_x10[idx] == 8u && batch->state.hitlag_pre_timer[idx] == 0u))
                ? 1u
                : 0u;
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF && batch->state.hitlag[idx] == 0u &&
            batch->state.prev_action_frame[idx] == 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u && guardsetoff_first_steady_x10_phase &&
            f221c == (uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2)) {
          // GuardSetOff first-steady reflect-window expiry:
          // - after prio-0 hitlag decrement, GuardSetOff_Anim / ftCo_80093BC0 owns the first steady
          //   post-hitlag row,
          // - the reflect-window bit x221C_b1 is tied to the x14 lane and should be gone once that
          //   timer has expired,
          // - keep this restricted to rows where the separate powershield-active x18 lane is also
          //   expired, so the destination no longer owns either GuardReflect timer,
          // - and where mv.co.guard.x10 is on the first steady countdown tick after entry. The
          //   canonical gx10==7 lane may be reached on the last hitlag-decrement row; the rollout
          //   gx10==8 variant must have started outside hitlag because last frozen gx10==8 rows
          //   retain x221C_b1 until the next steady callback tick.
          // - so on the first steady GuardSetOff row, clear the stale x221C_b1 carry whenever the
          //   destination still exposes both bits after both timer owners have ended.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
          // data/common/ft_common_data.json: guard_x10_init_frames
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B1;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            prev_action == (uint16_t)MSL_ACT_GUARD_SET_OFF && batch->state.hitlag[idx] == 0u &&
            batch->state.prev_action_frame[idx] > 0 && batch->state.action_frame[idx] > 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u && guard_x10_init != 0u &&
            (uint16_t)batch->state.guard_x10[idx] + 1u < (uint16_t)guard_x10_init &&
            f221c == (uint8_t)MSL_STATE_FLAG_221C_B2) {
          // GuardSetOff steady powershield-active expiry:
          // - ftCo_80092F2C seeds mv.co.guard.x10 from ftCommonData on GuardSetOff entry,
          // - after the initial af0 carry snapshot has already self-looped back into GuardSetOff,
          //   the next steady callback pass carries a positive prev_action_frame into
          //   ftCo_GuardSetOff_Anim / ftCo_80093BC0 ownership, and
          // - when both GuardReflect timers are already expired, x221C_b2 no longer has a live
          //   owner on that in-place GuardSetOff steady row and should not persist from the seeded
          //   carry row.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80092F2C,ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_800925A4}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          // data/common/ft_common_data.json: guard_x10_init_frames
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            prev_action == (uint16_t)MSL_ACT_GUARD_SET_OFF && batch->state.hitlag[idx] == 0u &&
            batch->state.prev_action_frame[idx] > 0 && batch->state.action_frame[idx] > 0 &&
            state_flags_guard_setoff_hitlag_handoff_phase(batch, idx) == 0u &&
            state_flags_guard_setoff_post_hitlag_owner(batch, idx) == 0u &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u && guard_x10_init != 0u &&
            (uint16_t)batch->state.guard_x10[idx] + 1u == (uint16_t)guard_x10_init &&
            f221c == (uint8_t)MSL_STATE_FLAG_221C_B2) {
          // GuardSetOff second-steady powershield-active expiry at the init-1 countdown:
          // - the first non-hitlag row after shield-hit can still expose x221C_b2 through the
          //   explicit post-hitlag owner lane,
          // - once that owner is gone, the in-place GuardSetOff self-loop is owned by
          //   GuardSetOff_Anim / ftCo_80093BC0, and expired x18 means no live x221C_b2 source
          //   remains even if mv.co.guard.x10 has not yet decremented below init-1.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_80092F2C}
          // data/common/ft_common_data.json: guard_x10_init_frames
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            (prev_action == (uint16_t)MSL_ACT_GUARD_SET_OFF ||
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF) &&
            (batch->state.prev_action_frame[idx] > 0 ||
             batch->state.seed_prev_action_frame[idx] > 0) &&
            batch->state.action_frame[idx] <= 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u && guard_x10_init != 0u &&
            ((uint16_t)batch->state.guard_x10[idx] + 1u == (uint16_t)guard_x10_init ||
             batch->state.guard_x10[idx] == guard_x10_init) &&
            f221c == (uint8_t)MSL_STATE_FLAG_221C_B2) {
          // GuardSetOff carry-snapshot powershield-active expiry:
          // - the prior steady GuardSetOff callback pass has already advanced (prev_action_frame>0),
          // - the destination snapshot has re-entered a carry/entry tick with nonpositive
          //   action_frame (`guard_x10 == guard_x10_init_frames - 1`, or a fresh GuardSetOff entry
          //   reset back to init), and
          // - once both GuardReflect timers are already expired, x221C_b2 no longer has a live
          //   owner on that in-place GuardSetOff carry snapshot.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80092F2C,ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_800925A4}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_ChangeMotionState}
          // data/common/ft_common_data.json: guard_x10_init_frames
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD &&
            prev_action == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
            batch->state.prev_action_frame[idx] > 0 && batch->state.action_frame[idx] <= 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u && guard_x10_init != 0u &&
            (uint16_t)batch->state.guard_x10[idx] + 2u == (uint16_t)guard_x10_init &&
            f221c == (uint8_t)MSL_STATE_FLAG_221C_B2) {
          // GuardSetOff -> Guard destination carry-snapshot expiry:
          // - ftCo_GuardSetOff_Anim first calls ftCo_80093BC0, then enters Guard through
          //   ftCo_800928CC when GuardDamage has ended.
          // - The destination Guard row can still expose the no-submotion carry snapshot, but once
          //   the GuardSetOff countdown had already advanced past the init tick before transition
          //   and both GuardReflect timers are expired, x221C_b2 has no live owner on that
          //   destination snapshot.
          // - The init-tick transition remains a valid carry lane; do not apply the in-place
          //   GuardSetOff first-steady clear after the state has already changed to Guard.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardSetOff_Anim,ftCo_80093BC0,ftCo_800928CC}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ChangeMotionState}
          // data/common/ft_common_data.json: guard_x10_init_frames
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        if (action_id == (uint16_t)MSL_ACT_GUARD_SET_OFF && batch->state.hitlag[idx] == 0u &&
            state_flags_guard_setoff_hitlag_handoff_phase(batch, idx) == 2u &&
            state_flags_guard_setoff_post_hitlag_owner(batch, idx) == 2u &&
            batch->state.prev_action_frame[idx] == 0 &&
            batch->state.guard_reflect_timer_x14[idx] == 0u &&
            batch->state.guard_reflect_timer_x18[idx] == 0u &&
            batch->state.lightshield_amount[idx] >= 0.999f &&
            batch->state.guard_setoff_hitlag_damage_min[idx] == 1u &&
            batch->state.guard_x10[idx] == 5u && f221c == (uint8_t)MSL_STATE_FLAG_221C_B2) {
          // GuardSetOff first-steady powershield-active expiry:
          // - after prio-0 hitlag decrement, GuardSetOff_Anim / ftCo_80093BC0 owns the first steady
          //   row,
          // - x221C_b2 persists only while the powershield-active x18 lane is live, and
          // - on the narrow full-lightshield, low-damage GuardSetOff rows where x18 is already
          //   expired and guard.x10 has advanced to the 5-frame steady window, clear the stale
          //   seeded x221C_b2 carry.
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B2;
        }
        if (state_flags_is_damage_action(action_id) && action_id != prev_action &&
            batch->state.action_frame[idx] == 1 && batch->state.hitlag[idx] > 0u &&
            batch->state.hitstun[idx] > 0u &&
            (f221c & (uint8_t)(MSL_STATE_FLAG_221C_B0 | MSL_STATE_FLAG_221C_IS_HITSTUN)) ==
                (uint8_t)(MSL_STATE_FLAG_221C_B0 | MSL_STATE_FLAG_221C_IS_HITSTUN)) {
          // Fresh Damage* destination entry owns fp->x221C_b0 clear:
          // - Fighter_ProcessHit can enter ftCo_8008DCE0 / ftCo_8008EC90 from non-Damage states or
          //   re-enter a different Damage* state while already damaged,
          // - Fighter_ChangeMotionState reset clears fp->x221C_b0 on that destination entry, while
          //   hitlag/hitstun for the new damage state remain active.
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B0;
        }
        if (action_id == (uint16_t)MSL_ACT_REBIRTH &&
            (prev_action == (uint16_t)MSL_ACT_DEAD_DOWN ||
             prev_action == (uint16_t)MSL_ACT_DEAD_LEFT ||
             prev_action == (uint16_t)MSL_ACT_DEAD_RIGHT ||
             prev_action == (uint16_t)MSL_ACT_DEAD_UP_STAR)) {
          // Rebirth entry owns the Fighter_ChangeMotionState reset bundle before the Rebirth
          // callback lane starts.
          // - Motion-state reset clears fp->x221C_b3 / b1 / b2.
          // - Death -> Rebirth enter runs through that reset before steady Rebirth callbacks.
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_800D4FF4
          f221c &= (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_B3 | MSL_STATE_FLAG_221C_B1 |
                                        MSL_STATE_FLAG_221C_B2);
        }
      }
      batch->state.state_flags[flags_221c_i] = f221c;

      // 0x221F_b1 transition ownership:
      // - KO start path sets fp->x221F_b1 = 1 (dead flow setup).
      // - Entry setup sets fp->x221F_b1 = 1 and Entry -> EntryStart keeps it for the intro flow.
      // - Generic motion-state reset clears fp->x221F_b1 on Dead{Down,Left,Right} -> Rebirth.
      // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3680
      // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C61B0
      // refs/melee/src/melee/ft/fighter.c (motion-state reset clears fp->x221F_b1)
      const size_t flags_221f_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221F_INDEX;
      if (state_flags_camera_target_live_pose_action(action_id) != 0u) {
        // Match-flow/dead-flow camera target owner:
        // - this is the F04/F25 lane where stale seeded camera subjects produce rollout-visible
        //   x221F_b0 drift,
        // - DamageFly* bottom-overlap timing is handled by its narrower existing owner below.
        // DeadUpFall/HitCamera uses ftCo_DeadUpFall_Cam -> ftCamera_80076320, which first refreshes
        // the camera target through ftCamera_UpdateCameraBox before applying the DeadUpFall camera
        // box center adjustment.
        // refs/melee/src/melee/ft/ft_0D4D.c::ftCo_DeadUpFall_Cam
        // refs/melee/src/melee/ft/ftcamera.c::{ftCamera_UpdateCameraBox,ftCamera_80076320}
        // refs/melee/src/melee/ft/ftlib.c::{ftLib_800866DC,ftLib_80086A8C}
        state_flags_refresh_camera_target_from_pose(batch, idx);
      }
      uint8_t f221f = batch->state.state_flags[flags_221f_i];
      // Entry->EntryStart runs through Fighter_ChangeMotionState reset paths; keep x221F_b1
      // transition-owned by explicit dead-flow setup and reset clear points rather than carrying it
      // across EntryStart snapshots.
      // refs/melee/src/melee/ft/ft_0C31.c::{ftCo_800C61B0,ftCo_800C6408}
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      if (state_flags_221f_dead_start_action(action_id) &&
          !state_flags_221f_dead_start_action(prev_action)) {
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      if (action_id == (uint16_t)MSL_ACT_DEAD_UP_STAR && c != NULL &&
          batch->state.match_flow_timer[idx] == (uint8_t)(c->dead_up_star_phase2_frames > 255u
                                                              ? 255u
                                                              : c->dead_up_star_phase2_frames)) {
        // DeadUpStar delayed dead-flow latch:
        // - the anim callback keeps a two-phase internal countdown (x508/x50C),
        // - match_flow_update_pre_anim has already decremented the shared countdown for this
        //   post-frame, and
        // - replay-visible post-frames pick up the x221F_b1 latch on the first frame with
        //   `dead_up_star_phase2_frames` remaining.
        // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
        // data/common/ft_common_data.json: dead_up_star_phase2_frames
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      if (state_flags_221f_dead_up_fall_hitcamera_action(action_id) != 0u && c != NULL &&
          batch->state.match_flow_timer[idx] <= (uint8_t)(c->dead_up_fall_phase4_frames > 255u
                                                              ? 255u
                                                              : c->dead_up_fall_phase4_frames)) {
        // DeadUpFallHitCamera phase-3 expiry latch:
        // - ftCo_DeadUpFall_Anim case 3 sets fp->x221F_b1 and calls ftCo_800D34E0,
        // - match_flow_update_pre_anim decrements the shared countdown and applies the stock-loss
        //   side effect at the same phase-3 -> phase-4 boundary, and
        // - replay-visible post-frames carry x221F_b1 from that boundary through the phase-4 hold.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D34E0}
        // data/common/ft_common_data.json: dead_up_fall_phase4_frames
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      if (action_id == (uint16_t)MSL_ACT_ENTRY_START) {
        // EntryStart snapshots follow Fighter_ChangeMotionState reset ownership; do not carry
        // seeded dead-flow x221F_b1 into this transition destination.
        // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6408
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        f221f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      if (action_id == (uint16_t)MSL_ACT_REBIRTH) {
        // Rebirth camera-box visibility lane:
        // - Rebirth_Cam updates the fighter camera subject after the first steady Rebirth frames.
        // - ftLib_80086A8C exposes fp->x221F_b0 from that camera subject visibility test.
        // - In suite-observed Rebirth entry rows, the first steady frame can transition from the
        //   pre-entry seed carry (0) into the visible camera-box lane (0x80).
        // refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
        // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
        if (batch->state.action_frame[idx] <= 2) {
          f221f |= (uint8_t)MSL_STATE_FLAG_221F_B0;
        }
        if (prev_action == (uint16_t)MSL_ACT_DEAD_UP_STAR && batch->state.action_frame[idx] == 0) {
          // DeadUpStar -> Rebirth transition frame:
          // - motion-state reset owns the destination snapshot,
          // - the Rebirth camera callback has not yet reasserted fp->x221F_b0 on this first
          //   transition post-frame.
          // refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
          f221f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221F_B0;
        }
        // Generic motion-state reset owns fp->x221F_b1 clear on Rebirth entry.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dead.c
        (void)prev_action;
        f221f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (action_id == (uint16_t)MSL_ACT_FALL_SPECIAL &&
          batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCO && ch != NULL &&
          !batch->state.camera_target_point_inside_stage_cam_bounds_u8[idx] &&
          state_flags_camera_below_stage_cam_bounds(batch, idx) &&
          state_flags_camera_overlap_stage_cam_bounds(batch, idx, 15.0f)) {
        // Falco FallSpecial off-screen bottom-overlap visibility set:
        // - ftLib_80086A8C sets fp->x221F_b0 when the camera-subject point is off-screen and the
        //   subject still overlaps the camera bounds through Camera_80030CFC(subject, 15).
        // - This currently stays scoped to Falco FallSpecial rows; Fox FallSpecial rows in-suite
        //   remain ref-clear under the same camera bounds while Falco's lower terminal-velocity tail
        //   reaches the overlap-visible branch.
        // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
        // refs/melee/src/melee/cm/camera.c::Camera_80030CFC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c
        // data/characters/falco.json: terminal_vel
        // data/stages/final_destination.json: cam_bounds_world
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B0;
      }
      if (action_id == (uint16_t)MSL_ACT_DAMAGE_FALL &&
          batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCO && ch != NULL &&
          !batch->state.camera_target_point_inside_stage_cam_bounds_u8[idx] &&
          state_flags_camera_below_stage_cam_bounds(batch, idx) &&
          state_flags_camera_overlap_stage_cam_bounds(batch, idx, 15.0f)) {
        // Falco DamageFall off-screen bottom-overlap visibility set:
        // - ftLib_80086A8C sets fp->x221F_b0 when the camera-subject point is off-screen and the
        //   subject still overlaps the camera bounds through Camera_80030CFC(subject, 15).
        // - Scope this to Falco DamageFall rows; the current suite's Fox DamageFall rows stay
        //   ref-clear under the same bottom-overlap geometry.
        // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
        // refs/melee/src/melee/cm/camera.c::Camera_80030CFC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
        // data/characters/falco.json: terminal_vel
        // data/stages/final_destination.json: cam_bounds_world
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B0;
      }
      if (state_flags_is_damage_fly_action(action_id) &&
          state_flags_camera_below_stage_cam_bounds(batch, idx) &&
          state_flags_camera_overlap_stage_cam_bounds(batch, idx, 15.0f)) {
        // DamageFly* bottom-overlap visibility set:
        // - ftLib_80086A8C sets fp->x221F_b0 when the camera target point is off-screen and the
        //   subject still overlaps the screen through Camera_80030CFC(subject, 15),
        // - keep this to the airborne DamageFly* family where the seeded camera target is already
        //   below the bottom camera bound but still within the radius+tolerance overlap window.
        // refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
        // refs/melee/src/melee/cm/camera.c::Camera_80030CFC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
        // data/stages/final_destination.json: cam_bounds_world
        f221f |= (uint8_t)MSL_STATE_FLAG_221F_B0;
      }
      batch->state.state_flags[flags_221c_i] = f221c;
      batch->state.state_flags[flags_221f_i] = f221f;
    }
  }
}

void state_flags_refresh_post_frame(MslBatch* batch) {
  state_flags_refresh_post_frame_impl(batch, NULL, 0);
}

void state_flags_refresh_post_frame_masked(MslBatch* batch, const uint8_t* mask_bytes,
                                           size_t mask_stride_bytes) {
  if (mask_bytes == NULL || mask_stride_bytes < sizeof(uint8_t)) {
    return;
  }
  state_flags_refresh_post_frame_impl(batch, mask_bytes, mask_stride_bytes);
}
