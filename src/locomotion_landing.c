#include "locomotion_landing.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_timebase.h"
#include "api.h"
#include "ids.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "stage_collision.h"
#include "state_flags.h"

static inline uint8_t landing_action_is_attackair(uint16_t a) {
  // Generated from decomp MotionState callback symbols ftCo_AttackAir_* for the five common
  // aerial attacks.
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  return msl_motion_state_common_class_has_fast(a, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint16_t landing_air_action_from_attackair(uint16_t a) {
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
      return (uint16_t)MSL_ACT_LANDING_AIR_N;
    case MSL_ACT_ATTACK_AIR_F:
      return (uint16_t)MSL_ACT_LANDING_AIR_F;
    case MSL_ACT_ATTACK_AIR_B:
      return (uint16_t)MSL_ACT_LANDING_AIR_B;
    case MSL_ACT_ATTACK_AIR_HI:
      return (uint16_t)MSL_ACT_LANDING_AIR_HI;
    case MSL_ACT_ATTACK_AIR_LW:
      return (uint16_t)MSL_ACT_LANDING_AIR_LW;
    default:
      return (uint16_t)MSL_ACT_LANDING;
  }
}

static inline uint8_t landing_action_owns_root_floor_snap(uint16_t land_act) {
  return msl_motion_state_common_class2_has_fast(land_act, MSL_MS_CLASS2_LANDING_ROOT_FLOOR_SNAP);
}

static inline uint32_t landing_submotion_for_action(uint8_t char_id, uint16_t a) {
  // Source of truth: every MotionState row carries the animation/submotion id selected by
  // Fighter_ChangeMotionState.
  // refs/melee/src/melee/ft/types.h::MotionState
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // data/motion_state/owners/*.bin::submotion_id_by_action
  const uint16_t sm = msl_motion_state_submotion_id(char_id, a);
  return sm != 0xFFFFu ? (uint32_t)sm : 0xFFFFFFFFu;
}

static inline void landing_entry_carry_raw_allow_interrupt_from_source(MslBatch* batch, size_t idx,
                                                                       uint16_t source_action,
                                                                       uint16_t land_action) {
  if (batch == NULL || land_action != (uint16_t)MSL_ACT_LANDING) {
    return;
  }
  if (move_tables_attackair_allow_interrupt(batch->state.char_id[idx], source_action,
                                            batch->state.anim_frame_f32[idx]) == 0u) {
    return;
  }
  // Raw fp+0x2218 bit0 carry on basic Landing entry:
  // - AttackAir scripts set fp->allow_interrupt through ftAction_80071950.
  // - ftCo_AttackAir_Coll can enter ftCo_Landing_Enter_Basic on auto-cancel floor contact.
  // - ftCo_Landing_Enter_Basic sets mv.co.landing.allow_interrupt=true, but does not clear the
  //   already-live raw fp->allow_interrupt bit before Slippi serializes state_flags[0].
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
  //   ftCo_Landing_Enter,ftCo_Landing_Enter_Basic}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
}

uint8_t locomotion_landing_contact_y_owner_matches_source(uint8_t char_id, uint16_t source_act,
                                                          uint16_t source_action_frame,
                                                          uint16_t prev_source_act,
                                                          uint16_t land_act) {
  if (msl_motion_state_fx_special_kind(char_id, source_act) == (uint8_t)MSL_FX_KIND_NONE &&
      source_act >= 341u) {
    // Char-special sources resolve through the extracted MotionState identity; other
    // characters' same-numbered specials have their own landing owners.
    return 0u;
  }
  const uint8_t prev_source_fx_kind = msl_motion_state_fx_special_kind(char_id, prev_source_act);
  const uint8_t landing_basic_prev =
      (prev_source_act == (uint16_t)MSL_ACT_JUMP_F || prev_source_act == (uint16_t)MSL_ACT_JUMP_B ||
       prev_source_act == (uint16_t)MSL_ACT_FALL ||
       prev_source_act == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
       (prev_source_fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_START &&
        prev_source_fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END))
          ? 1u
          : 0u;

  if (source_act == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
      source_act == (uint16_t)MSL_ACT_ATTACK_AIR_LW) {
    return 1u;
  }
  if (source_act == (uint16_t)MSL_ACT_ATTACK_AIR_F) {
    if (land_act == (uint16_t)MSL_ACT_LANDING) {
      return 1u;
    }
    if (land_act == (uint16_t)MSL_ACT_LANDING_AIR_F && source_action_frame >= 16u) {
      // Narrow runtime slice: late-window AttackAirF -> LandingAirF rows only.
      // Frame gate tie-down (ISO-extracted script events):
      // - data/moves/fox.json   moves["ftCo_SM_AttackAirF"]["events"] contains create_hitbox at frame 16.
      // - data/moves/falco.json moves["ftCo_SM_AttackAirF"]["events"] contains create_hitbox at frame 16.
      return 1u;
    }
  }

  // Additional ft_80082B1C -> Landing_Enter_Basic callback family:
  // - Fall collision callback path (Fall -> Landing), including lanes that have already entered
  //   Landing before this resolver (source_act==Landing with prev_source_act==Fall).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // - Jump ground-collision callback path (JumpF/B -> Landing).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
  // - Aerial jump collision callback path (JumpAerialB -> Landing in observed suite rows).
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
  // - Fox/Falco Blaster aerial collision path via AirCatchHit (SpecialAirN* -> Landing).
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //     ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll
  //   }
  //   refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  if (land_act == (uint16_t)MSL_ACT_LANDING &&
      (source_act == (uint16_t)MSL_ACT_FALL || source_act == (uint16_t)MSL_ACT_JUMP_F ||
       source_act == (uint16_t)MSL_ACT_JUMP_B || source_act == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
       (msl_motion_state_fx_special_kind(char_id, source_act) >=
            (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_START &&
        msl_motion_state_fx_special_kind(char_id, source_act) <=
            (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END) ||
       ((source_act == (uint16_t)MSL_ACT_LANDING) && landing_basic_prev))) {
    return 1u;
  }

  return 0u;
}

static inline uint8_t landing_contact_is_ledge_floor(const MslBatch* batch, size_t idx, size_t bi) {
  if (batch == NULL || !batch->state.on_ground[idx]) {
    return 0u;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  return g->lines[(size_t)line_idx].is_ledge ? 1u : 0u;
}

float locomotion_landing_root_y_from_mpcoll_contact(const MslBatch* batch, size_t idx, size_t bi,
                                                    uint8_t preserve_fall_basic_dd90_order) {
  // Decomp owner:
  // - mpLib_8004DD90_Floor applies a +0.0001 root/floor bias to its returned correction.
  // - Some lite mpColl helper paths store the floor plane in `ground_contact_y`; direct DD90 paths
  //   store the already-biased projected root. Normalize the scratch lane before Landing* entry so
  //   the post-collision state does not apply the DD90 bias twice.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  static const float k_mplib_floor_y_bias = 0.0001f;
  if (batch == NULL || batch->state.ground_id[idx] == 0xFFFFu) {
    return batch != NULL ? batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias : 0.0f;
  }

  const uint32_t stage_id = batch->state.stage_id[bi];
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  if (stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
      stage_collision_floor_line_height_platform_state_is_source_trusted(
          batch, (int)bi, batch->state.ground_id[idx])) {
    // grIzumi height-platform collision publishes the transformed floor plane through the current
    // mpColl result; Landing* entry still owns the final mpLib_8004DD90_Floor root bias.
    //
    // Source/data owner:
    // - grIzumi refreshes height-platform JObjs before the fighter map callback.
    // - MSLSTG01 platform_transforms(kind=height) identifies the line as a live grIzumi height
    //   owner, and `stage_fod_platform_height_source` marks the current sparse-seed contact/source
    //   lane as trusted for this frame.
    // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    uint8_t platform_id = 0u;
    const uint8_t same_step_contact_source =
        (stage_collision_floor_line_platform_transform_id(stage_id, batch->state.ground_id[idx],
                                                          &platform_id) &&
         platform_id < 2u &&
         (batch->state.stage_fod_platform_height_source[bi * 2u + (size_t)platform_id] &
          (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u)
            ? 1u
            : 0u;
    if (same_step_contact_source != 0u) {
      // Same-step grIzumi platform contact stores the callback-local transformed floor result
      // before Landing* entry performs its final mpLib_8004DD90_Floor root publication. AttackAir
      // collision (`ftCo_AttackAir_Coll` -> `ft_80082C74` -> `mpColl_800471F8`) and aerial
      // side-special end collision (`ftFx_SpecialAirSEnd_Coll` -> `ft_CheckGroundAndLedge`) and
      // SpecialHiFall landing store the already-biased root in the scratch lane; JumpF/B landing
      // stores the callback-local transformed floor result and needs the entry publication bias
      // here. Direct replay rows EWT:9157/10353/10984 and PTE:9136/10272 lock the callback-family
      // split; normal current-source platform carry without SAME_STEP_CONTACT stays on the
      // single-bias path.
      // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{ftCo_JumpF_Coll,ftCo_JumpB_Coll}
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      const uint16_t pre_action = batch->state.action_id[idx];
      if (pre_action >= (uint16_t)MSL_ACT_ATTACK_AIR_N &&
          pre_action <= (uint16_t)MSL_ACT_ATTACK_AIR_LW) {
        return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
      }
      const uint8_t pre_kind =
          msl_motion_state_fx_special_kind(batch->state.char_id[idx], pre_action);
      if (pre_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END ||
          pre_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_LANDING ||
          pre_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL) {
        return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
      }
      return batch->state.ground_contact_y[idx] + (2.0f * k_mplib_floor_y_bias);
    }
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  MslStageFloorLine world = {0};
  if (!stage_collision_floor_line_world(batch, (int)bi, &g->lines[(size_t)line_idx], &world)) {
    return batch->state.ground_contact_y[idx] + k_mplib_floor_y_bias;
  }

  float floor_y = world.y0;
  if (fabsf(world.x1 - world.x0) > 0.0001f) {
    floor_y = world.y0 + ((world.y1 - world.y0) * (batch->state.pos_x[idx] - world.x0) /
                          (world.x1 - world.x0));
  }
  if (preserve_fall_basic_dd90_order != 0u && batch->state.coll_floor_result_source[idx] == 1u &&
      batch->state.coll_floor_result_mode[idx] == 1u &&
      isfinite(batch->state.coll_substep_cur_pos_y[idx])) {
    // Source-order mpLib_8004DD90_Floor publication for Fall_Coll -> ft_80082B1C basic Landing:
    // ft_80083090_inline adds the signed DD90 correction back to `coll->cur_pos.y`; it does not
    // algebraically collapse the expression to `floor_y + 0.0001`. Keep this explicit caller gate
    // so AttackAir/DamageAir landing owners with different callback-local publication contracts do
    // not inherit the Fall-family f32 order from scratch-lane shape alone.
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpCollEnd}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_80082B1C}
    const float source_y = batch->state.coll_substep_cur_pos_y[idx];
    const float y_corr = (floor_y - source_y) + k_mplib_floor_y_bias;
    return source_y + y_corr;
  }
  if (!g->lines[(size_t)line_idx].is_platform &&
      !stage_collision_floor_line_has_platform_transform(stage_id, batch->state.ground_id[idx]) &&
      fabsf(world.y1 - world.y0) > 0.0001f) {
    // Generated/static slope landing-entry owner:
    // The collision callback has already accepted the current floor id before entering Landing*.
    // When the scratch contact_y is stale, source `mpLib_8004DD90_Floor` still projects the root
    // onto the accepted sloped floor line. Use the data-backed line plane for static hard-floor
    // slopes rather than preserving the stale flat contact.
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter
    // data/stages/bin/*.bin::MSLSTG01 floor line endpoints
    return floor_y + k_mplib_floor_y_bias;
  }

  const float contact_y = batch->state.ground_contact_y[idx];
  const float dist_to_floor = fabsf(contact_y - floor_y);
  const float dist_to_biased_floor = fabsf(contact_y - (floor_y + k_mplib_floor_y_bias));
  if (dist_to_biased_floor < dist_to_floor) {
    return contact_y;
  }
  if (dist_to_floor <= k_mplib_floor_y_bias) {
    return contact_y + k_mplib_floor_y_bias;
  }
  return contact_y;
}

uint8_t locomotion_action_uses_ft80082b1c_basic_landing_callback(uint8_t char_id, uint16_t a) {
  // Generated from decomp MotionState collision callback symbols:
  // - Fall_Coll -> ft_800831CC(..., ft_80082B1C)
  // - Jump/JumpAerial_Coll -> ft_800835B0(..., ft_80082B1C)
  // - CliffJump2_Coll -> ft_800835B0(..., ft_80082B1C)
  // - Fox/Falco SpecialAirN* collision callbacks -> ft_80082B1C
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_800831CC,ft_800835B0}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
  return msl_motion_state_class_has(char_id, a, MSL_MS_CLASS_FT80082B1C_BASIC_LANDING_COLL);
}

uint16_t locomotion_ft80082b1c_basic_landing_action(const MslBatch* batch, const MslCommonParams* c,
                                                    size_t idx, uint16_t source_act) {
  if (!locomotion_action_uses_ft80082b1c_basic_landing_callback(batch->state.char_id[idx],
                                                                source_act)) {
    return (uint16_t)MSL_ACT_LANDING;
  }
  // ft_80082B1C keeps gentle floor contact in the neutral grounded state when vertical self
  // velocity is above the scaled threshold, otherwise it enters Landing_Enter_Basic.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  const float scale_y = (batch != NULL && batch->state.fighter_scale_y[idx] > 0.0f)
                            ? batch->state.fighter_scale_y[idx]
                            : 1.0f;
  return msl_ftco_80082b1c_enters_wait(c, scale_y, batch->state.speed_y_self[idx])
             ? (uint16_t)MSL_ACT_WAIT
             : (uint16_t)MSL_ACT_LANDING;
}

uint16_t locomotion_attackair_landing_action_for_contact(const MslBatch* batch, size_t idx,
                                                         uint16_t a) {
  if (!landing_action_is_attackair(a)) {
    return 0u;
  }
  // AttackAir_Coll dispatches through ft_80082C74 even on same-frame hitlag contacts; the callback
  // then lets ftCo_LandingAir_EnterWithLag choose LandingAir* vs autocancel Landing from
  // fp->cmd_vars[0]. cmd_vars[0] is modeled from extracted MSLFTSC1 set_cmd_var timelines.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  const uint8_t lag_enabled = move_tables_attackair_cmd0_active(batch->state.char_id[idx], a,
                                                                batch->state.anim_frame_f32[idx]);
  return lag_enabled ? landing_air_action_from_attackair(a) : (uint16_t)MSL_ACT_LANDING;
}

void locomotion_enter_landing_action_from_air(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                              size_t bi, uint16_t source_act, uint16_t land_act) {
  if (batch == NULL || ch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();

  // Landed this frame.
  // Decomp grounding keeps self_vel.x and gr_vel aligned on ground entry:
  // - ftCommon_8007D6A4 sets `fp->gr_vel = fp->self_vel.x` (does not zero self_vel.x).
  //   refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
  // - Ground update keeps `fp->self_vel.x` synced from `fp->gr_vel` each frame.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  //
  // Keep both seed/output lanes synchronized at landing entry:
  // - speed_ground_x_self <-> fp->gr_vel
  // - speed_air_x_self <-> fp->self_vel.x
  const float landing_self_vel_x = batch->state.speed_air_x_self[idx];
  batch->state.speed_ground_x_self[idx] = landing_self_vel_x;
  batch->state.speed_air_x_self[idx] = landing_self_vel_x;
  // Landing-entry root-Y ownership:
  // - Airborne collision callbacks resolve floor contact before entering Landing / LandingAir* /
  //   LandingFallSpecial through ftCommon_8007D7FC + Fighter_ChangeMotionState.
  // - mpLib_8004DD90_Floor projects onto the owning floor line and applies the grounded +0.0001
  //   bias; replay-visible post-frame rows therefore own the collision floor root position on the
  //   destination landing state, not the pre-contact airborne root.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{ftCo_Landing_Enter_Basic,ftCo_LandingFallSpecial_Enter}
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  uint8_t apply_contact_y_owner =
      (batch->state.on_ground[idx] && landing_action_owns_root_floor_snap(land_act)) ? 1u : 0u;
  if (apply_contact_y_owner && source_act == (uint16_t)MSL_ACT_FALL &&
      landing_contact_is_ledge_floor(batch, idx, bi)) {
    // Keep edge/walk-off positioning owned by mpColl on ledge floor segments.
    // Decomp shape:
    // - Fall collision callback routes through ft_80082B1C.
    // - mpColl floor-edge snap/ownership is handled in mpColl_8004A45C_Floor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
    apply_contact_y_owner = 0u;
  }
  if (apply_contact_y_owner) {
    const uint8_t preserve_fall_basic_dd90_order =
        (source_act == (uint16_t)MSL_ACT_FALL && land_act == (uint16_t)MSL_ACT_LANDING) ? 1u : 0u;
    batch->state.pos_y[idx] = locomotion_landing_root_y_from_mpcoll_contact(
        batch, idx, bi, preserve_fall_basic_dd90_order);
  }
  if (source_act == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      land_act == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
      batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
      batch->state.frame_start_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
      batch->state.coll_floor_result_valid[idx] != 0u) {
    // The official Sheik demo proves a same-proc KneeBend -> Jump -> EscapeAir floor hit where
    // LandingFallSpecial publishes the first mpColl substep root, not the fully integrated airborne
    // root. Keep this on the live frame-start KneeBend owner: replay seed history is too stale and
    // falsely catches ordinary Fox/Falco EscapeAir landing tails in aggregate validation.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    batch->state.pos_x[idx] = 0.5f * (batch->state.prev_pos_x[idx] + batch->state.pos_x[idx]);
  }

  batch->state.fall_fast[idx] = 0;
  // Decomp: grounding transitions clear ECB lock via ftCommon_UnlockECB.
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D6A4,ftCommon_UnlockECB}
  batch->state.ecb_lock_timer[idx] = 0u;

  // Jump refresh is tied to explicit landing-enter transitions only (not raw on_ground flips).
  //
  // Decomp: ftCommon_8007D6A4 sets `fp->x1968_jumpsUsed = 0` when the fighter becomes grounded,
  // which refreshes jumps remaining back to max_jumps.
  // refs/melee/src/melee/ft/ftcommon.c:556-573
  //
  // Slippi post-frame `jumps` is "jumps left" (see Recording/SendGamePostFrame.asm), so:
  // jumps_left = max_jumps - jumps_used.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  batch->state.jumps_left[idx] = ch->max_jumps;

  batch->state.action_id[idx] = land_act;
  batch->state.animation_index[idx] =
      landing_submotion_for_action(batch->state.char_id[idx], land_act);
  landing_entry_carry_raw_allow_interrupt_from_source(batch, idx, source_act, land_act);
  if (land_act == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    // Decomp: LandingFallSpecial carries mv.co.landing.allow_interrupt from its entry helper.
    // EscapeAir_Coll passes false; FallSpecial_Coll forwards the FallSpecial source flag. This
    // runtime path models the suite-owned sources we currently enter explicitly.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    const uint8_t fallspecial_allow = batch->state.landing_fallspecial_allow_interrupt[idx];
    batch->state.landing_fallspecial_allow_interrupt[idx] =
        (source_act == (uint16_t)MSL_ACT_FALL_SPECIAL ||
         source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
         source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_B)
            ? fallspecial_allow
            : 0u;
  } else {
    batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
  }

  // Decomp: Fighter_ChangeMotionState sets:
  // - fp->frame_speed_mul = anim_speed
  // - fp->cur_anim_frame = anim_start - fp->frame_speed_mul
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
  //
  // LandingAir additionally calls ftAnim_SetAnimRate to adjust fp->frame_speed_mul without
  // adjusting fp->cur_anim_frame. refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
  //
  // LandingFallSpecial passes a scaled anim_speed directly. refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
  const uint8_t cid = batch->state.char_id[idx];
  if (land_act == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    const float landing_lag = (c != NULL) ? c->landing_fall_special_lag_frames : 0.0f;
    const float end_frame = msl_anim_end_frame(cid, (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
    float speed = 1.0f;
    if (source_act == (uint16_t)MSL_ACT_ESCAPE_AIR ||
        (msl_motion_state_fx_special_kind(batch->state.char_id[idx], source_act) ==
         (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END)) {
      // Source-specific LandingFallSpecial rate:
      // - EscapeAir_Coll enters LandingFallSpecial with p_ftCommonData->x344.
      // - SpecialAirSEnd_Coll enters LandingFallSpecial with the Illusion/Phantasm landing lag.
      // - FallSpecial_Coll forwards mv.co.fallspecial.landing_lag written by ftCo_80096900.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
      speed = (landing_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / landing_lag) : 1.0f;
    } else if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_MARTH &&
               (source_act == (uint16_t)MSL_ACT_MS_SPECIAL_HI ||
                source_act == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_HI)) {
      // Dolphin Slash landing: ftMs_SpecialHi_Coll -> ftMs_SpecialHi_80138884 enters
      // LandingFallSpecial with MarsAttributes x2C.
      // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_80138884
      const MslCharParams* ms_ch = msl_char_params_fast(batch->state.char_id[idx]);
      const float ms_lag = (ms_ch != NULL) ? ms_ch->specialhi_landing_lag_frames : 0.0f;
      speed = (ms_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / ms_lag) : 1.0f;
    } else if (source_act == (uint16_t)MSL_ACT_FALL_SPECIAL ||
               source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
               source_act == (uint16_t)MSL_ACT_FALL_SPECIAL_B) {
      const float source_lag = batch->state.fallspecial_landing_lag[idx];
      speed = (source_lag > 0.0f && end_frame > 0.0f) ? ((end_frame + 0.1f) / source_lag) : 1.0f;
    }
    msl_anim_timebase_enter(batch, idx, 0.0f, speed);
  } else {
    // Most motion states enter with anim_speed=1.0 and anim_start=0.0.
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);

    // LandingAir*: set anim rate so the timeline finishes in `lag` frames.
    if (land_act == (uint16_t)MSL_ACT_LANDING_AIR_N ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_F ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_B ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_HI ||
        land_act == (uint16_t)MSL_ACT_LANDING_AIR_LW) {
      uint8_t lag_frames = 0;
      switch (land_act) {
        case (uint16_t)MSL_ACT_LANDING_AIR_N:
          lag_frames = ch->landing_airn_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_F:
          lag_frames = ch->landing_airf_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_B:
          lag_frames = ch->landing_airb_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_HI:
          lag_frames = ch->landing_airhi_lag_frames;
          break;
        case (uint16_t)MSL_ACT_LANDING_AIR_LW:
          lag_frames = ch->landing_airlw_lag_frames;
          break;
        default:
          lag_frames = 0;
          break;
      }

      float lag = (float)lag_frames;
      uint8_t did_lcancel = 0;
      // Decomp: landing lag is divided when x67F < p_ftCommonData->xE4.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
      if (c != NULL && lag > 0.0f && batch->state.lr_press_timer[idx] < c->lcancel_window_frames) {
        did_lcancel = 1;
        const float div_lag = lag / c->lcancel_lag_div;
        int int_lag = (int)div_lag;
        if (int_lag == 0) {
          int_lag = 1;
        }
        lag = (float)int_lag;
      }

      // Slippi post-frame `l_cancel` is a 1-frame status emitted on LandingAir* entry:
      // - 0: not applicable / no lag landing.
      // - 1: successful L-cancel.
      // - 2: missed L-cancel.
      //
      // Decomp tie-down for the success condition: fp->x67F < p_ftCommonData->xE4.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
      // refs/melee/src/melee/ft/fighter.c:2078-2086 (x67F update; see src/input.c)
      batch->state.l_cancel[idx] = (lag_frames > 0) ? (uint8_t)(did_lcancel ? 1 : 2) : 0;

      const uint32_t sm = landing_submotion_for_action(batch->state.char_id[idx], land_act);
      const float end_frame = (sm <= 0xFFFFu) ? msl_anim_end_frame(cid, (uint16_t)sm) : 0.0f;
      if (lag > 0.0f && end_frame > 0.0f) {
        const float rate = (end_frame + 0.1f) / lag;
        msl_anim_timebase_set_rate(batch, idx, rate);
      }
    }
  }
}
