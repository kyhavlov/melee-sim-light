#include "mpcoll_source_air.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_timebase.h"
#include "anim_pose.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "ecb_pose.h"
#include "ftcommon_ecb.h"
#include "grab_attachment.h"
#include "knockdown.h"
#include "match_flow.h"
#include "locomotion.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "move_tables.h"
#include "mp_coll.h"
#include "mp_lib.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_env.h"
#include "mpcoll_floor_skip.h"
#include "sheik_specials.h"
#include "spacie_specials.h"
#include "stage_collision.h"
#include "throw_flow.h"

enum { MSL_SOURCE_AIR_MAX_SUBSTEPS = 255 };
static const float k_source_substep_extent = 6.0f;

static uint8_t selector_has_air_wrapper(uint8_t selector) {
  switch ((MslCollWrapperSelectorKind)selector) {
    case MSL_COLL_SELECTOR_AIR_471F8:
    case MSL_COLL_SELECTOR_AIR_LEDGE_FACING:
    case MSL_COLL_SELECTOR_AIR_LEDGE_BOTH:
    case MSL_COLL_SELECTOR_AIR_DAMAGE:
    case MSL_COLL_SELECTOR_AIR_CALLBACK_LEDGE:
    case MSL_COLL_SELECTOR_AIR_PASSIVEWALL_TIMER:
    case MSL_COLL_SELECTOR_AIR_STOPCEIL:
    case MSL_COLL_SELECTOR_AIR_FLYREFLECT:
    case MSL_COLL_SELECTOR_AIR_48160:
    case MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED:
    case MSL_COLL_SELECTOR_GA_DAMAGE:
    case MSL_COLL_SELECTOR_GA_CAPTURECUT:
    case MSL_COLL_SELECTOR_GA_CATCHCUT:
    case MSL_COLL_SELECTOR_GA_CLIFF_ACTION:
    case MSL_COLL_SELECTOR_GA_THROW:
    case MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM:
    case MSL_COLL_SELECTOR_MATCH_REBIRTH:
    case MSL_COLL_SELECTOR_MATCH_REBIRTH_WAIT:
    case MSL_COLL_SELECTOR_GA_B108_AIR471:
    case MSL_COLL_SELECTOR_GA_B2DC_AIR471:
    case MSL_COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH:
    case MSL_COLL_SELECTOR_FALCON_LW_END:
    case MSL_COLL_SELECTOR_MARS_HI:
    case MSL_COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL:
      return 1u;
    default:
      return 0u;
  }
}

typedef struct MslSourceAirCollData {
  float last_x;
  float last_y;
  float prev_x;
  float prev_y;
  float cur_x;
  float cur_y;
  MslEcbWorldPoints prev_ecb;
  MslEcbWorldPoints ecb;
  MslEcbWorldPoints desired_ecb;
  MslEcbWorldPoints restore_ecb;
  uint16_t floor_id;
  uint16_t floor_skip;
  uint16_t left_wall_id;
  uint16_t right_wall_id;
  uint32_t env_flags;
  uint32_t prev_env_flags;
  uint8_t squeezed;
  uint8_t touched_floor;
  uint8_t floor_contact;
  uint8_t floor_contact_soft;
  uint8_t stay_airborne;
  uint8_t can_grab_ledge;
  uint8_t hard_floor_only;
} MslSourceAirCollData;

static uint8_t source_air_callback(MslBatch* batch, int bi, int p, uint8_t selector_override,
                                   uint8_t geometry_only, uint8_t clear_ecb);

static inline float source_abs_max(float a, float b) {
  const float aa = fabsf(a);
  const float bb = fabsf(b);
  return aa > bb ? aa : bb;
}

static inline void source_ecb_rebuild(MslEcbWorldPoints* ecb, float x, float y) {
  mpcoll_ecb_world_points_from_rel(ecb, x, y, ecb->bottom_rel_y, ecb->top_rel_y, ecb->left_rel_x,
                                   ecb->right_rel_x, ecb->side_rel_y, ecb->frame_u16);
}

static inline void source_ecb_interpolate(MslEcbWorldPoints* out, const MslEcbWorldPoints* desired,
                                          float x, float y, float time) {
  out->bottom_rel_y += time * (desired->bottom_rel_y - out->bottom_rel_y);
  out->top_rel_y += time * (desired->top_rel_y - out->top_rel_y);
  out->left_rel_x += time * (desired->left_rel_x - out->left_rel_x);
  out->right_rel_x += time * (desired->right_rel_x - out->right_rel_x);
  out->side_rel_y += time * (desired->side_rel_y - out->side_rel_y);
  out->frame_u16 = desired->frame_u16;
  source_ecb_rebuild(out, x, y);
}

static inline uint8_t source_uses_vanish_jobj_ecb(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[idx];
  const uint16_t action_id = batch->state.action_id[idx];
  const MslCharParams* ch = msl_char_params_fast(char_id);
  // Sheik/Zelda Vanish rows are identified by extracted callback identity plus their shared
  // character-data landing owner. This excludes spacie SpecialHi, whose XRotN JObj recipe is
  // different, without branching on character id or a local action list.
  // data/characters/{sheik,zelda}.json::sheik_vanish_landing_lag_frames
  // data/motion_state/owners/{sheik,zelda}.bin::MSLMSO01 class_bits
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Coll
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialHi.c::ftZd_SpecialAirHi_Coll
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  return (uint8_t)(ch != NULL && ch->sheik_vanish_landing_lag_frames > 0.0f &&
                   batch->state.live_coll_wrapper_selector_kind[idx] ==
                       (uint8_t)MSL_COLL_SELECTOR_AIR_LEDGE_FACING &&
                   msl_motion_state_fx_special_kind(char_id, action_id) ==
                       (uint8_t)MSL_FX_KIND_NONE);
}

static void source_apply_passive_wall_jobj_ecb(MslEcbWorldPoints* ecb, float root_x, float root_y) {
  if (ecb == NULL) {
    return;
  }
  // ftCo_PassiveWall_Coll selects ft_80083318 -> mpColl_80047F40, whose 0xA load flags keep the
  // ledge-enabled JObj span but force the horizontal ECB to exactly +/-1. The narrow packet is the
  // defining difference from the ordinary airborne 473CC wrapper; without it, startup rows are
  // spuriously pushed off nearby wall shells.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083318
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047F40,mpColl_LoadECB_JObj}
  ecb->left_rel_x = -1.0f;
  ecb->right_rel_x = 1.0f;
  if (ecb->bottom_rel_y < 0.0f) {
    ecb->bottom_rel_y = 0.0f;
  }
  source_ecb_rebuild(ecb, root_x, root_y);
}

static void source_apply_jobj_height_two(MslEcbWorldPoints* ecb, float root_x, float root_y) {
  if (ecb == NULL) {
    return;
  }
  float bottom = 0.5f * (ecb->bottom_rel_y + ecb->top_rel_y) - 1.0f;
  if (bottom < 0.0f) {
    bottom = 0.0f;
  }
  ecb->bottom_rel_y = bottom;
  ecb->top_rel_y = bottom + 2.0f;
  ecb->side_rel_y = bottom + 1.0f;
  source_ecb_rebuild(ecb, root_x, root_y);
}

static void source_apply_selector_jobj_mode(const MslBatch* batch, size_t idx, uint8_t selector,
                                            MslEcbWorldPoints* ecb, float root_x, float root_y) {
  const uint16_t action = batch->state.action_id[idx];
  if (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_48160 ||
      selector == (uint8_t)MSL_COLL_SELECTOR_GA_CLIFF_ACTION ||
      (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_PASSIVEWALL_TIMER &&
       batch->state.passivewall_timer[idx] != 0u) ||
      (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_FLYREFLECT &&
       action == (uint16_t)MSL_ACT_FLY_REFLECT_WALL)) {
    // mpColl_80048160/48274/48464, airborne Cliff actions, and the live PassiveWall timer branch
    // use flags 0xA.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048160,mpColl_80048274,mpColl_80048464}
    source_apply_passive_wall_jobj_ecb(ecb, root_x, root_y);
  } else if (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_STOPCEIL ||
             (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_FLYREFLECT &&
              action == (uint16_t)MSL_ACT_FLY_REFLECT_CEIL)) {
    // StopCeil and FlyReflectCeil use flags 0x12: a two-unit JObj vertical envelope.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047D20,mpColl_80048388,mpColl_80048578}
    source_apply_jobj_height_two(ecb, root_x, root_y);
  } else if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_CAPTURECUT ||
             selector == (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH ||
             selector == (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH_WAIT) {
    // flags 5 keeps ledge width and grounds the JObj bottom at the fighter root.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800474E0,mpColl_800478F4,mpColl_80048654}
    ecb->bottom_rel_y = 0.0f;
    source_ecb_rebuild(ecb, root_x, root_y);
  }
}

static void source_load_ecb(MslBatch* batch, size_t idx, uint8_t selector,
                            MslSourceAirCollData* coll) {
  const uint8_t char_id = batch->state.char_id[idx];
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
  if (anim > 0xFFFFu || msl_ecb_world_points_sample_collision_pose_f32(
                            &coll->desired_ecb, batch, idx, char_id, (uint16_t)anim, (float)frame,
                            facing, coll->cur_x, coll->cur_y, 0u) != 0) {
    msl_ecb_world_points_sample(&coll->desired_ecb, char_id, anim, frame, facing, coll->cur_x,
                                coll->cur_y, 0u);
  }
  // ProcessHit runs after the outgoing action's map callback, so the first Damage hitlag callback
  // starts with that callback's CollData.current packet. Fighter_ChangeMotionState evaluates the
  // destination pose immediately, so mpColl_LoadECB_inline loads that new JObj pose into desired
  // even though the later per-frame Anim callback remains hitlag-gated. Keep the lanes distinct.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
  if (anim <= 0xFFFFu &&
      mpcoll_common_fall_blended_ecb_live_owner(batch, idx, batch->state.action_id[idx])) {
    MslEcbWorldPoints blended_ecb = coll->desired_ecb;
    uint16_t neutral_msid = 0u;
    uint16_t forward_msid = 0u;
    uint16_t backward_msid = 0u;
    if (msl_action_common_fall_blend_msids(batch->state.action_id[idx], &neutral_msid,
                                           &forward_msid, &backward_msid) &&
        mpcoll_common_fall_blended_ecb_points(&blended_ecb, batch, idx, char_id, neutral_msid,
                                              frame, facing, coll->cur_x, coll->cur_y)) {
      // Fall/FallAerial's Anim callback writes the live blend used by the subsequent map callback;
      // the source kernel must not replace it with a discrete animation-frame envelope.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_Anim
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
      coll->desired_ecb = blended_ecb;
    }
  } else if (mpcoll_ground_specialhi_uses_jobj_ecb(char_id, batch->state.action_id[idx])) {
    MslEcbWorldPoints jobj_ecb = coll->desired_ecb;
    if (mpcoll_ground_try_sample_specialhi_jobj_ecb(&jobj_ecb, batch, idx, char_id, anim,
                                                    batch->state.action_id[idx], frame, facing,
                                                    coll->cur_x, coll->cur_y)) {
      coll->desired_ecb = jobj_ecb;
    }
  } else if (mpcoll_ground_damageflyroll_uses_jobj_ecb(batch->state.action_id[idx])) {
    MslEcbWorldPoints jobj_ecb = coll->desired_ecb;
    if (mpcoll_ground_try_sample_damageflyroll_jobj_ecb(&jobj_ecb, batch, idx, char_id, anim,
                                                        batch->state.action_id[idx], frame, facing,
                                                        coll->cur_x, coll->cur_y)) {
      coll->desired_ecb = jobj_ecb;
    }
  } else if (source_uses_vanish_jobj_ecb(batch, idx)) {
    MslEcbWorldPoints jobj_ecb = coll->desired_ecb;
    if (mpcoll_vanish_jobj_ecb_points(&jobj_ecb, batch, idx, char_id, (uint16_t)anim, frame, facing,
                                      coll->cur_x, coll->cur_y)) {
      coll->desired_ecb = jobj_ecb;
    }
  }
  source_apply_selector_jobj_mode(batch, idx, selector, &coll->desired_ecb, coll->cur_x,
                                  coll->cur_y);
  if (batch->state.ecb_lock_timer[idx] != 0u) {
    const float locked_bottom = batch->state.coll_desired_ecb_bottom_valid[idx] != 0u
                                    ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
                                    : 0.0f;
    // ftCommon_8007D5D4 enters the ordinary ground-to-air lock with the grounded ECB bottom at
    // root height. Dedicated owners serialize a different bottom explicitly; otherwise zero is
    // the real source default, not the destination animation's lifted airborne bottom.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
    msl_ecb_world_points_preserve_desired_bottom_rel_y(&coll->desired_ecb, coll->cur_x, coll->cur_y,
                                                       locked_bottom);
  }
  const uint8_t had_current_ecb = mpcoll_state_current_ecb_points(
      batch, idx, &coll->ecb, coll->last_x, coll->last_y, msl_ecb_prev_frame_u16(frame));
  if (!had_current_ecb) {
    const uint16_t prev_frame = msl_ecb_prev_frame_u16(frame);
    if (anim > 0xFFFFu || msl_ecb_world_points_sample_collision_pose_f32(
                              &coll->ecb, batch, idx, char_id, (uint16_t)anim, (float)prev_frame,
                              facing, coll->last_x, coll->last_y, 0u) != 0) {
      msl_ecb_world_points_sample(&coll->ecb, char_id, anim, prev_frame, facing, coll->last_x,
                                  coll->last_y, 0u);
    }
    source_apply_selector_jobj_mode(batch, idx, selector, &coll->ecb, coll->last_x, coll->last_y);
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM) {
    MslEcbWorldPoints custom = {0};
    if (match_flow_entry_custom_ecb(batch, idx, &custom)) {
      coll->desired_ecb = custom;
    }
  }
  if (mpcoll_ground_specialhi_uses_jobj_ecb(char_id, batch->state.action_id[idx]) &&
      batch->state.floor_sweep_prev_runtime_owned[idx] == 0u) {
    MslEcbWorldPoints jobj_ecb = coll->ecb;
    if (mpcoll_ground_try_sample_specialhi_jobj_ecb(
            &jobj_ecb, batch, idx, char_id, anim, batch->state.action_id[idx],
            msl_ecb_prev_frame_u16(frame), facing, coll->last_x, coll->last_y)) {
      coll->ecb = jobj_ecb;
    }
  } else if (mpcoll_ground_damageflyroll_uses_jobj_ecb(batch->state.action_id[idx]) &&
             batch->state.floor_sweep_prev_runtime_owned[idx] == 0u) {
    MslEcbWorldPoints jobj_ecb = coll->ecb;
    if (mpcoll_ground_try_sample_damageflyroll_jobj_ecb(
            &jobj_ecb, batch, idx, char_id, anim, batch->state.action_id[idx],
            msl_ecb_prev_frame_u16(frame), facing, coll->last_x, coll->last_y)) {
      // A replay seed is a completed frame-end DamageFlyRoll pose but does not serialize the
      // XRotN-mutated JObj ECB. Reconstruct that source-owned current packet once; live callbacks
      // retain the dynamic packet published by source_publish.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doFlyRoll,ftCo_DamageFlyRoll_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpCollInterpolateECB}
      coll->ecb = jobj_ecb;
    }
  } else if (source_uses_vanish_jobj_ecb(batch, idx) &&
             batch->state.floor_sweep_prev_runtime_owned[idx] == 0u && anim <= 0xFFFFu) {
    MslEcbWorldPoints jobj_ecb = coll->ecb;
    if (mpcoll_vanish_jobj_ecb_points(&jobj_ecb, batch, idx, char_id, (uint16_t)anim,
                                      msl_ecb_prev_frame_u16(frame), facing, coll->last_x,
                                      coll->last_y)) {
      coll->ecb = jobj_ecb;
    }
  }
  if (!had_current_ecb && batch->state.ecb_lock_timer[idx] != 0u) {
    const float locked_bottom = batch->state.coll_desired_ecb_bottom_valid[idx] != 0u
                                    ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
                                    : 0.0f;
    msl_ecb_world_points_preserve_desired_bottom_rel_y(&coll->ecb, coll->last_x, coll->last_y,
                                                       locked_bottom);
  }
  coll->prev_ecb = coll->ecb;
  if (mpcoll_state_squeeze_restore_ecb_points(batch, idx, &coll->restore_ecb, coll->last_x,
                                              coll->last_y, msl_ecb_prev_frame_u16(frame))) {
    coll->squeezed = 1u;
    batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
  }
}

static void source_publish(MslBatch* batch, size_t idx, const MslSourceAirCollData* coll) {
  const size_t bi = idx / (size_t)MSL_MAX_PLAYERS;
  batch->state.pos_x[idx] = coll->cur_x;
  batch->state.pos_y[idx] = coll->cur_y;
  batch->state.on_ground[idx] = coll->touched_floor;
  batch->state.ground_id[idx] = coll->floor_id;
  batch->state.coll_prev_env_flags[idx] = coll->prev_env_flags;
  batch->state.coll_env_flags[idx] = coll->env_flags;
  batch->state.coll_last_pos_x[idx] = coll->cur_x;
  batch->state.coll_last_pos_y[idx] = coll->cur_y;
  batch->state.coll_substep_prev_pos_x[idx] = coll->prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = coll->prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = coll->cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = coll->cur_y;
  mpcoll_store_prev_ecb_points(batch, idx, &coll->prev_ecb);
  mpcoll_store_current_ecb_points(batch, idx, &coll->ecb);
  mpcoll_store_desired_ecb_points(batch, idx, &coll->desired_ecb);
  batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_geometry_generation[idx] = batch->state.stage_collision_geometry_generation[bi];
  batch->state.coll_floor_result_valid[idx] = coll->floor_contact;
  batch->state.coll_floor_result_source[idx] =
      coll->floor_contact ? (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT : 0u;
  batch->state.coll_floor_result_mode[idx] =
      coll->floor_contact ? (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP : 0u;
  batch->state.coll_floor_result_segment_id[idx] = coll->floor_id;
  batch->state.coll_floor_result_contact_x[idx] = batch->state.ground_contact_x[idx];
  batch->state.coll_floor_result_contact_y[idx] = batch->state.ground_contact_y[idx];
  batch->state.coll_floor_result_normal_x[idx] = batch->state.ground_normal_x[idx];
  batch->state.coll_floor_result_normal_y[idx] = batch->state.ground_normal_y[idx];
  if (batch->state.hitlag[idx] != 0u && batch->state.coll_damage_hitlag_ecb_valid[idx] != 0u &&
      coll->floor_contact != 0u) {
    batch->state.coll_damage_hitlag_floor_contact_runtime[idx] = 1u;
  } else if (batch->state.hitlag[idx] == 0u) {
    batch->state.coll_damage_hitlag_floor_contact_runtime[idx] = 0u;
    batch->state.coll_damage_hitlag_ecb_valid[idx] = 0u;
  }
}

static void source_air_keep_action_grounded(MslBatch* batch, size_t idx) {
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  msl_ftcommon_8007d6a4(batch, ch, idx);
}

static uint8_t source_air_capture_hi_floor_continuation(MslBatch* batch, size_t idx) {
  const uint16_t action = batch->state.action_id[idx];
  uint16_t next_action = 0u;
  uint16_t next_submotion = 0u;
  switch (action) {
    case MSL_ACT_CAPTURE_PULLED_HI:
      next_action = (uint16_t)MSL_ACT_CAPTURE_PULLED_LW;
      next_submotion = (uint16_t)MSL_SM_CAPTURE_PULLED_LW;
      break;
    case MSL_ACT_CAPTURE_WAIT_HI:
      next_action = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
      next_submotion = (uint16_t)MSL_SM_CAPTURE_WAIT_LW;
      break;
    case MSL_ACT_CAPTURE_DAMAGE_HI:
      next_action = (uint16_t)MSL_ACT_CAPTURE_DAMAGE_LW;
      next_submotion = (uint16_t)MSL_SM_CAPTURE_DAMAGE_LW;
      break;
    default:
      return 0u;
  }

  const float frame = batch->state.anim_frame_f32[idx];
  source_air_keep_action_grounded(batch, idx);
  batch->state.action_id[idx] = next_action;
  batch->state.animation_index[idx] = (uint32_t)next_submotion;
  msl_anim_timebase_enter(batch, idx, frame, 1.0f);
  // CapturePulledHi/WaitHi/DamageHi all run the constrained ft_80083C00 -> 477E0 wrapper and
  // invoke their family-specific Hi->Lw callback when FloorMask is published. Keep that callback
  // in the installed map owner; a later grab-flow probe cannot reproduce CollData or fighter proc
  // ordering.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledHi_Coll,fn_800DAEEC,
  //   ftCo_CaptureWaitHi_Coll,fn_800DBBF8,
  //   ftCo_CaptureDamageHi_Coll,fn_800DC404}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083C00
  return 1u;
}

static void source_air_enter_stop_ceil(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  const float old_pos_y = batch->state.pos_y[idx];
  const float old_ecb_top =
      batch->state.coll_ecb_bottom_valid[idx] != 0u ? batch->state.coll_ecb_top_rel_y[idx] : 0.0f;
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_STOP_CEIL, (uint32_t)MSL_SM_STOP_CEIL, 0u,
                      0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
  float transn[3] = {0.0f, 0.0f, 0.0f};
  (void)anim_pose_get_transn(batch->state.char_id[idx], (uint16_t)MSL_SM_STOP_CEIL, 0u, transn);
  batch->state.pos_y[idx] = old_pos_y + old_ecb_top + transn[1];
  if (source_air_callback(batch, bi, p, 0u, 1u, 0u)) {
    (void)locomotion_source_air_floor_contact(batch, bi, idx, (uint8_t)MSL_COLL_HANDLER_AIR_COMMON);
  }
  batch->state.speed_y_self[idx] = 0.0f;
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_StopCeil.c::ftCo_8009EFA4
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082D40
}

static uint8_t source_air_selector_continuation(MslBatch* batch, int bi, int p, uint8_t selector,
                                                const MslSourceAirCollData* coll) {
  const size_t idx = msl_idx_player(bi, p);
  if (coll->floor_contact) {
    if (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED &&
        source_air_capture_hi_floor_continuation(batch, idx)) {
      return 1u;
    }
    if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_CAPTURECUT ||
        selector == (uint8_t)MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM ||
        selector == (uint8_t)MSL_COLL_SELECTOR_GA_CLIFF_ACTION) {
      source_air_keep_action_grounded(batch, idx);
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::fn_800DC8FC
      // refs/melee/src/melee/ft/ft_0C31.c::fn_800C63BC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AE14
      return 1u;
    }
    if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_CATCHCUT) {
      return locomotion_source_air_floor_contact(batch, bi, idx,
                                                 (uint8_t)MSL_COLL_HANDLER_AIR_COMMON);
    }
    if (selector == (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH_WAIT) {
      match_flow_rebirth_wait_floor_contact(batch, bi, p);
      return 1u;
    }
    if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_THROW) {
      if (batch->state.throw_coll_x4[idx] != 0u || batch->state.throw_coll_x8[idx] == 0u) {
        if (batch->state.throw_coll_x4[idx] == 0u) {
          // The pre-hold hard-floor continuation advances the throw script until cmd_vars[0]
          // becomes live. The retail loop is bounded by the finite throw script; keep the same
          // fixed-cap runtime shape here.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::fn_800DD5EC
          for (int i = 0; i < 255; i++) {
            if (move_tables_special_cmd_var_value_at_frame(
                    batch->state.char_id[idx], batch->state.animation_index[idx], 0u,
                    batch->state.anim_frame_f32[idx]) != 0u) {
              break;
            }
            msl_anim_timebase_tick_once(batch, idx);
          }
        } else {
          batch->state.frame_speed_mul_fp_q16_16[idx] = (int32_t)MSL_Q16_16_ONE;
          msl_anim_timebase_tick_once(batch, idx);
        }
        batch->state.frame_speed_mul_fp_q16_16[idx] = (int32_t)MSL_Q16_16_ONE;
        // Both DD568 and DD5EC invoke the installed Throw Anim callback after advancing the script,
        // while x4/x8 still describe the pre-continuation state. This can consume a release/facing
        // flag or exit an ended throw in the same callback.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{fn_800DD568,fn_800DD5EC}
        throw_flow_update_anim_callback_pre_input(batch, bi, p);
        batch->state.throw_coll_x4[idx] = 0u;
        batch->state.throw_coll_x8[idx] = 1u;
        throw_flow_resume_attached_victim_after_hold(batch, bi, p);
        source_air_keep_action_grounded(batch, idx);
      }
      return 1u;
    }
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_CLIFF_ACTION &&
      (coll->env_flags & (uint32_t)MSL_COLLIDE_CEILING_HUG) != 0u) {
    source_air_enter_stop_ceil(batch, bi, p);
    return 1u;
  }
  return 0u;
}

static uint8_t source_air_callback(MslBatch* batch, int bi, int p, uint8_t selector_override,
                                   uint8_t geometry_only, uint8_t clear_ecb) {
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t selector = selector_override != 0u
                               ? selector_override
                               : batch->state.live_coll_wrapper_selector_kind[idx];
  if (selector == (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH &&
      !match_flow_rebirth_stage_collision_runs(batch, bi)) {
    return 0u;
  }
  const uint32_t plan = batch->state.live_coll_source_plan[idx];
  MslSourceAirCollData coll = {0};
  // ft_081B wrappers copy CollData.cur_pos to last_pos immediately before publishing the current
  // fighter root. The source endpoint is therefore the previous callback's live `cur_pos`, not a
  // second replay-row displacement reconstructed by validation tooling.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80081D0C,ft_CheckGroundAndLedge}
  coll.last_x = isfinite(batch->state.coll_last_pos_x[idx]) ? batch->state.coll_last_pos_x[idx]
                                                            : batch->state.prev_pos_x[idx];
  coll.last_y = isfinite(batch->state.coll_last_pos_y[idx]) ? batch->state.coll_last_pos_y[idx]
                                                            : batch->state.prev_pos_y[idx];
  coll.cur_x = batch->state.pos_x[idx];
  coll.cur_y = batch->state.pos_y[idx];
  coll.floor_id = batch->state.ground_id[idx];
  const uint16_t callback_floor_id = coll.floor_id;
  coll.floor_skip = batch->state.floor_skip_segment_id[idx];
  const uint8_t callback_wall_kind = batch->state.wall_kind[idx];
  const uint16_t callback_wall_id = batch->state.wall_id[idx];
  const uint8_t callback_wall_live = batch->state.coll_wall_commit_runtime[idx];
  coll.left_wall_id = 0xFFFFu;
  coll.right_wall_id = 0xFFFFu;
  coll.prev_env_flags = batch->state.coll_env_flags[idx];
  coll.env_flags = 0u;
  const uint8_t handler = batch->state.live_coll_handler_kind[idx];
  const uint8_t blocked_ledge_wrapper =
      (uint8_t)(batch->state.ledge_cooldown[idx] != 0u || batch->state.dmg_x2224_b2[idx] != 0u);
  coll.stay_airborne =
      (uint8_t)(selector == (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED ||
                selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL ||
                selector == (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH ||
                selector == (uint8_t)MSL_COLL_SELECTOR_GA_THROW);
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_THROW) {
    coll.stay_airborne = 1u;
    coll.hard_floor_only =
        (uint8_t)(batch->state.throw_coll_x4[idx] == 0u && batch->state.throw_coll_x8[idx] == 0u);
  }
  coll.can_grab_ledge = (uint8_t)((selector == (uint8_t)MSL_COLL_SELECTOR_AIR_LEDGE_FACING ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_AIR_LEDGE_BOTH ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_AIR_CALLBACK_LEDGE ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_AIR_PASSIVEWALL_TIMER ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_AIR_STOPCEIL ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_AIR_FLYREFLECT ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_GA_CAPTURECUT ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_GA_CATCHCUT ||
                                   selector == (uint8_t)MSL_COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH) &&
                                  !blocked_ledge_wrapper);
  const uint8_t fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]);
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
    // SpecialAirHi consumes the floor mask while remaining airborne unless its own rebound
    // predicate enters Bound. mpColl still applies the contact correction and publishes FloorMask.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    coll.stay_airborne = 1u;
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_DAMAGE ||
      selector == (uint8_t)MSL_COLL_SELECTOR_GA_DAMAGE) {
    // ft_80081DD4 selects the low-level wrapper from live Fighter state, not from MotionState
    // identity alone: active SDI uses 477E0 (stay airborne), cooldown/forced-state uses 471F8,
    // and the ordinary path uses 473CC (ledge enabled). The extracted callback identity owns this
    // procedural choice; the action row cannot encode one fixed wrapper.
    // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    coll.stay_airborne = batch->state.damage_allow_sdi[idx] ? 1u : 0u;
    coll.can_grab_ledge = (uint8_t)(!coll.stay_airborne && batch->state.ledge_cooldown[idx] == 0u &&
                                    batch->state.dmg_x2224_b2[idx] == 0u);
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_FLYREFLECT &&
      batch->state.damage_allow_sdi[idx] != 0u) {
    coll.stay_airborne = 1u;
    coll.can_grab_ledge = 0u;
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_MARS_HI) {
    // Dolphin Slash uses 477E0 while rising and for the first descending command frame. Only the
    // subsequent descending callback selects ft_800831CC, which admits floor landing, wall-jump,
    // and cliff catch. The command latch is written before that first 477E0 call.
    // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::{ftMs_SpecialHi_Coll,
    // ftMs_SpecialAirHi_Coll}
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80082578,ft_800831CC,ft_80083B68}
    // cmd0 is an extracted fighter-script variable, not a Marth runtime approximation. Seeded
    // snapshots cannot serialize cmd_vars and the source-complete Marth physics owner consumes the
    // same script event without duplicating it into special_cmd0, so recover the live value from
    // MSLFTSC1 here at the callback that reads it. Free-running and teacher-forced execution then
    // share the exact same owner.
    // data/scripts/marth.bin::MSLFTSC1
    // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::{ftMs_SpecialHi_Coll,
    //   ftMs_SpecialAirHi_Coll}
    const uint8_t launched = move_tables_special_cmd_var_value_at_frame(
        batch->state.char_id[idx], batch->state.animation_index[idx], 0u,
        batch->state.anim_frame_f32[idx]);
    const uint8_t descending = (uint8_t)(launched != 0u && batch->state.speed_y_self[idx] < 0.0f);
    const uint8_t late = (uint8_t)(descending && batch->state.special_cmd1[idx] != 0u);
    coll.stay_airborne = (uint8_t)!late;
    coll.can_grab_ledge = late;
    if (descending && !late) {
      batch->state.special_cmd1[idx] = 1u;
    }
  }
  // The explicit replay lane serializes CollData's wall index plus the prior WallHug callback
  // phase. DamageFly consumes that state outside hitlag as well; hitlag is not an ownership gate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044E10_RightWall,mpColl_80045B74_LeftWall}
  const uint8_t seeded_damage_wall =
      (uint8_t)(msl_coll_handler_is_damage(handler) &&
                ((callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_LEFT &&
                  (coll.prev_env_flags & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u) ||
                 (callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_RIGHT &&
                  (coll.prev_env_flags & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u)));
  const uint8_t moving_away_from_runtime_wall =
      (uint8_t)(callback_wall_live && ((callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_LEFT &&
                                        coll.cur_x > coll.last_x + 0.0001f) ||
                                       (callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_RIGHT &&
                                        coll.cur_x < coll.last_x - 0.0001f)));
  if (callback_wall_id != 0xFFFFu && (callback_wall_live || seeded_damage_wall) &&
      !moving_away_from_runtime_wall) {
    if (callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_LEFT) {
      coll.left_wall_id = callback_wall_id;
    } else if (callback_wall_kind == (uint8_t)MSL_MPCOLL_WALL_KIND_RIGHT) {
      coll.right_wall_id = callback_wall_id;
    }
  }
  batch->state.wall_id[idx] = 0xFFFFu;
  batch->state.wall_kind[idx] = 0u;
  batch->state.coll_wall_commit_runtime[idx] = 0u;
  batch->state.ceiling_id[idx] = 0xFFFFu;
  batch->state.coll_wall_probe_valid[idx] = 0u;
  batch->state.coll_wall_probe_commit_kind[idx] = 0u;
  batch->state.coll_wall_probe_candidate_count[idx] = 0u;
  batch->state.coll_wall_probe_segment_id[idx] = -1;
  batch->state.coll_wall_probe_corr_x[idx] = 0.0f;
  source_load_ecb(batch, idx, selector, &coll);
  if (clear_ecb) {
    // DDDE4 sets CollData_X130_Clear before loading the released fighter's ECB. LoadECB clears the
    // current packet to a zero envelope, retains the freshly sampled desired packet, and then the
    // ordinary subdivision driver interpolates between them.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043670,mpColl_LoadECB_JObj,
    //   mpColl_LoadECB_Fixed}
    coll.ecb.bottom_rel_y = 0.0f;
    coll.ecb.top_rel_y = 0.0f;
    coll.ecb.left_rel_x = 0.0f;
    coll.ecb.right_rel_x = 0.0f;
    coll.ecb.side_rel_y = 0.0f;
    source_ecb_rebuild(&coll.ecb, coll.last_x, coll.last_y);
    coll.prev_ecb = coll.ecb;
    coll.squeezed = 0u;
    batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
  }

  float max_delta = source_abs_max(coll.cur_x - coll.last_x, coll.cur_y - coll.last_y);
  max_delta = fmaxf(max_delta, source_abs_max(coll.desired_ecb.left_rel_x - coll.ecb.left_rel_x,
                                              coll.desired_ecb.right_rel_x - coll.ecb.right_rel_x));
  max_delta = fmaxf(max_delta, source_abs_max(coll.desired_ecb.top_rel_y - coll.ecb.top_rel_y,
                                              coll.desired_ecb.side_rel_y - coll.ecb.side_rel_y));
  int steps =
      max_delta > k_source_substep_extent ? (int)(max_delta / k_source_substep_extent) + 1 : 1;
  if (steps > MSL_SOURCE_AIR_MAX_SUBSTEPS) {
    steps = MSL_SOURCE_AIR_MAX_SUBSTEPS;
  }
  const float callback_cur_x = coll.cur_x;
  const float callback_cur_y = coll.cur_y;
  const float dx = (coll.cur_x - coll.last_x) / (float)steps;
  const float dy = (coll.cur_y - coll.last_y) / (float)steps;
  coll.cur_x = coll.last_x;
  coll.cur_y = coll.last_y;
  coll.touched_floor = 0u;
  uint8_t outer_stop = 0u;
  for (int step = 0; step < steps && !outer_stop; step++) {
    coll.prev_x = coll.cur_x;
    coll.prev_y = coll.cur_y;
    coll.prev_ecb = coll.ecb;
    if (coll.squeezed) {
      coll.ecb = coll.restore_ecb;
      coll.squeezed = 0u;
      batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
    }
    coll.cur_x += dx;
    coll.cur_y += dy;
    source_ecb_interpolate(&coll.ecb, &coll.desired_ecb, coll.cur_x, coll.cur_y,
                           1.0f / (float)(steps - step));
    source_ecb_rebuild(&coll.prev_ecb, coll.prev_x, coll.prev_y);
    MslMpCollFrame contact = {
        .prev_x = coll.prev_x,
        .prev_y = coll.prev_y,
        .cur_x = coll.cur_x,
        .cur_y = coll.cur_y,
        .prev_ecb = coll.prev_ecb,
        .ecb = coll.ecb,
        .desired_ecb = coll.desired_ecb,
        .prev_env_flags = coll.prev_env_flags,
        .env_flags = coll.env_flags,
        .left_wall_id = coll.left_wall_id,
        .right_wall_id = coll.right_wall_id,
        .floor_id = coll.floor_id,
        .squeezed = 0u,
    };
    const MslMpCollAirStepResult step_result = msl_mpcoll_resolve_air_step(
        batch, bi, idx, &contact, coll.floor_skip, coll.stay_airborne,
        msl_coll_source_plan_has(plan, MSL_COLL_SOURCE_FLOOR_CALLBACK_PLATFORM_PASS),
        coll.hard_floor_only);
    // mpColl_80043754 publishes the current geometry generation after each completed collision
    // substep. Only the first substep after a moving-stage update enters the mpCheck*Remap family.
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
    batch->state.coll_geometry_generation[idx] =
        batch->state.stage_collision_geometry_generation[bi];
    coll.left_wall_id = contact.left_wall_id;
    coll.right_wall_id = contact.right_wall_id;
    coll.cur_x = contact.cur_x;
    coll.cur_y = contact.cur_y;
    coll.ecb = contact.ecb;
    coll.desired_ecb = contact.desired_ecb;
    coll.env_flags = contact.env_flags;
    coll.squeezed = contact.squeezed;
    if (coll.squeezed) {
      // x64_ecb is the packet saved immediately before the first squeeze. It remains live until
      // the next interpolation step restores it; if this was the callback's final substep it is
      // published as persistent CollData state for the next frame.
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,
      //   mpCollSqueezeHorizontal,mpCollSqueezeVertical}
      (void)mpcoll_state_squeeze_restore_ecb_points(batch, idx, &coll.restore_ecb, coll.cur_x,
                                                    coll.cur_y, coll.ecb.frame_u16);
    }
    if (step_result.floor_contact) {
      coll.floor_id = step_result.floor_id;
      coll.floor_contact = 1u;
      coll.floor_contact_soft = step_result.floor_contact_soft;
    }
    if (step_result.touched_floor) {
      coll.touched_floor = 1u;
    }
    outer_stop = step_result.outer_stop;
  }
  if (!geometry_only && coll.touched_floor && coll.cur_x != callback_cur_x &&
      msl_action_is_thrown_victim(batch->state.frame_start_action_id[idx]) &&
      (coll.env_flags & (uint32_t)(MSL_COLLIDE_LEFT_WALL_MASK | MSL_COLLIDE_RIGHT_WALL_MASK)) ==
          0u) {
    // DDDE4 has already published the release-local 471F8 packet before throw damage enters its
    // destination motion. When the destination DamageFly callback finds that carried floor while
    // advancing the ECB, mpColl_80044838_Floor projects the completed post-Phys Fighter.cur_pos;
    // the sweep intersection is contact geometry, not a replacement horizontal fighter root.
    // Finish the unobstructed X displacement and reproject Y on the accepted floor. Other air
    // callback families retain their existing collision-substep ownership.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor,
    //   mpColl_80044838_Floor}
    MslMpLibFloorProjection endpoint = {0};
    const float projection_y =
        coll.ecb.bottom_rel_y > 0.0f ? coll.cur_y : coll.cur_y + coll.ecb.bottom_rel_y;
    if (msl_mplib_project_floor(batch, bi, coll.floor_id, callback_cur_x, projection_y,
                                &endpoint)) {
      coll.cur_x = callback_cur_x;
      coll.cur_y += endpoint.correction_y;
      coll.floor_id = endpoint.line_id;
      source_ecb_rebuild(&coll.ecb, coll.cur_x, coll.cur_y);
      batch->state.ground_contact_x[idx] = endpoint.contact_x;
      batch->state.ground_contact_y[idx] = endpoint.contact_y;
      batch->state.ground_normal_x[idx] = endpoint.normal_x;
      batch->state.ground_normal_y[idx] = endpoint.normal_y;
    }
  }
  source_publish(batch, idx, &coll);
  if (geometry_only) {
    return coll.touched_floor;
  }
  const uint8_t selector_continued =
      source_air_selector_continuation(batch, bi, p, selector, &coll);
  if (selector_continued) {
    // The exact installed callback continuation returns before the generic landing/walljump/cliff
    // ladder.
  } else if (coll.floor_contact && fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    if (coll.floor_contact_soft) {
      msl_mpcoll_update_floor_skip(batch, idx, coll.floor_id);
    } else if (spacie_specialhi_floor_contact_should_bound(batch, ch, idx)) {
      // The hard-floor IsBound continuation publishes the floor plane as the bound action's root;
      // the stay-airborne 471F8 envelope used to detect contact must not leave the root one ECB
      // bottom below that plane.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_IsBound
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044838_Floor,mpCollFloorInline}
      batch->state.pos_y[idx] = batch->state.ground_contact_y[idx] + 0.0001f;
      batch->state.coll_last_pos_y[idx] = batch->state.pos_y[idx];
      spacie_enter_specialhi_bound_from_airhi_collision(batch, ch, idx);
    } else {
      spacie_specialhi_apply_collision_facing_dir(batch, ch, idx);
    }
  } else if (coll.touched_floor && coll.floor_contact_soft &&
             sheik_special_vanish_air_start1_platform_pass_active(batch, idx)) {
    // The early travel window in Sheik/Zelda's aerial teleport callback consumes an accepted
    // soft-floor contact through ftCo_8009A134: mpColl's corrected root/floor publication remains
    // live, but the fighter stays airborne and the contacted line becomes CollData.floor_skip.
    // This callback-owned gate is distinct from the ordinary down-stick platform-pass recipe.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_1_Coll
    // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialHi.c::ftZd_SpecialAirHiStart_1_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    msl_mpcoll_update_floor_skip(batch, idx, coll.floor_id);
    batch->state.on_ground[idx] = 0u;
    if (coll.floor_id == callback_floor_id) {
      // mpColl_80044628_Floor's same-line result is consumed as a pass-through without retaining
      // its temporary projection. An adjacent-line remap, however, has already published the new
      // collision root and remains visible to the callback (the Battlefield transition case).
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_8004A908}
      batch->state.pos_y[idx] = callback_cur_y;
    }
  } else if (coll.touched_floor) {
    (void)locomotion_source_air_floor_contact(batch, bi, idx, handler);
  } else if (locomotion_try_installed_walljump_post_collision(batch, idx)) {
    // Source ft_081B wrappers return immediately after ftWallJump_8008169C succeeds; cliff catch
    // is not evaluated for the displaced callback.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_80083464,ft_800835B0,ft_8008370C}
  } else if (coll.can_grab_ledge) {
    // ft_CheckGroundAndLedge runs the cliff mask inside this callback's mpColl substep, before the
    // callback returns to Fighter_procMap. The post-map CliffCatch transition still consumes the
    // resulting CollData flags in fighter callback order.
    // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
    mpcoll_env_update_ledge_grab_one(batch, bi, p, coll.last_x, coll.last_y, coll.cur_x,
                                     coll.cur_y);
  }
  knockdown_update_post_collision_one(batch, bi, p);
  return coll.floor_contact;
}

uint8_t mpcoll_source_air_run_installed_callback(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t selector = batch->state.live_coll_wrapper_selector_kind[idx];
  if (batch->state.on_ground[idx] != 0u ||
      !match_flow_should_stage_collide(batch->state.action_id[idx]) ||
      !grab_attachment_map_callback_runs(batch, idx) || !selector_has_air_wrapper(selector) ||
      (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED &&
       batch->state.grab_constraint_x2226_b2[idx] != 0u) ||
      (selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL &&
       batch->state.falcon_specialhi_x221b_b7[idx] != 0u)) {
    return 0u;
  }
  batch->state.live_coll_callback_ran[idx] = 1u;
  return source_air_callback(batch, bi, p, 0u, 0u, 0u);
}

uint8_t mpcoll_source_air_run_release_471f8(MslBatch* batch, int bi, int p, float last_x,
                                            float last_y) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players || !isfinite(last_x) || !isfinite(last_y)) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t live_ga = batch->state.on_ground[idx];
  batch->state.coll_last_pos_x[idx] = last_x;
  batch->state.coll_last_pos_y[idx] = last_y;
  const uint8_t floor_contact =
      source_air_callback(batch, bi, p, (uint8_t)MSL_COLL_SELECTOR_AIR_471F8, 1u, 1u);
  // DDDE4 calls ftCommon_8007D5D4 before 471F8. mpColl publishes CollData floor contact but does
  // not run the released fighter's newly installed Coll callback or change ground_or_air.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  batch->state.on_ground[idx] = live_ga;
  return floor_contact;
}

uint8_t mpcoll_source_air_run_dc920_fallback(MslBatch* batch, int bi, int p, float last_x,
                                             float last_y, uint8_t grounded) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players || !isfinite(last_x) || !isfinite(last_y)) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  batch->state.coll_last_pos_x[idx] = last_x;
  batch->state.coll_last_pos_y[idx] = last_y;
  const uint8_t selector = grounded ? (uint8_t)MSL_COLL_SELECTOR_MATCH_REBIRTH_WAIT
                                    : (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED;
  return source_air_callback(batch, bi, p, selector, 1u, 1u);
}

void mpcoll_source_air_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.live_coll_callback_ran[idx] != 0u || batch->state.on_ground[idx] != 0u ||
          !match_flow_should_stage_collide(batch->state.action_id[idx])) {
        continue;
      }
      const uint8_t selector = batch->state.live_coll_wrapper_selector_kind[idx];
      if (!selector_has_air_wrapper(selector) ||
          (selector == (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED &&
           batch->state.grab_constraint_x2226_b2[idx] != 0u) ||
          (selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL &&
           batch->state.falcon_specialhi_x221b_b7[idx] != 0u)) {
        continue;
      }
      batch->state.live_coll_callback_ran[idx] = 1u;
      (void)source_air_callback(batch, bi, p, 0u, 0u, 0u);
    }
  }
}
