#include "reflector_bubbles.h"
#include "motion_state_owners.h"
#include "char_registry.h"
#include "ids.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"
#include "fighter_pose.h"
#include "shield_tilt_table.h"

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_ID_FOX) || (char_id == (uint8_t)MSL_CHAR_ID_FALCO);
}

static inline uint8_t action_is_zelda_nayru(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_ZD_SPECIAL_N || a == (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_N);
}

static inline uint8_t action_is_shine_reflector_active(uint8_t char_id, uint16_t a) {
  // Suite-confirmed: Slippi reflect-active bit 0x10 is set in Loop/Hit/Turn but not Start/End.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwLoop_Enter (CreateReflectHit)
  switch (msl_motion_state_fx_special_kind(char_id, a)) {
    case MSL_FX_KIND_SPECIAL_LW_LOOP:
    case MSL_FX_KIND_SPECIAL_LW_HIT:
    case MSL_FX_KIND_SPECIAL_LW_TURN:
    case MSL_FX_KIND_SPECIAL_AIR_LW_LOOP:
    case MSL_FX_KIND_SPECIAL_AIR_LW_HIT:
    case MSL_FX_KIND_SPECIAL_AIR_LW_TURN:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_shine(uint8_t char_id, uint16_t a) {
  // Ownership from the extracted MotionState row identity (the full Reflector family,
  // ftFx_MS_SpecialLwStart .. ftFx_MS_SpecialAirLwTurn); other characters' same-numbered
  // specials stay kind 0. Kind values are contiguous in table order.
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, a);
  return (fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
          fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_TURN)
             ? 1u
             : 0u;
}

static inline uint8_t action_is_guard_reflector_owner(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_GUARD_ON || a == (uint16_t)MSL_ACT_GUARD ||
                   a == (uint16_t)MSL_ACT_GUARD_REFLECT || a == (uint16_t)MSL_ACT_GUARD_SET_OFF);
}

void reflector_bubbles_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      const MslCharParams* ch = msl_char_params_fast(cid);
      const uint16_t a = batch->state.action_id[idx];
      const uint16_t prev_a = batch->state.prev_action_id[idx];

      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      uint8_t f = batch->state.state_flags[flags_i];
      const uint8_t zelda_nayru_active =
          (uint8_t)(cid == (uint8_t)MSL_CHAR_ID_ZELDA && action_is_zelda_nayru(a) &&
                    batch->state.special_cmd0[idx] == 2u);

      // Slippi state_flags byte0 bit0x10 mirrors fp->reflecting (GALE01 fp+0x2218 bit4).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit (sets fp->reflecting=true)
      //
      // Policy:
      // - Override this bit for Shine (SpecialLw) because we model the reflector bubble lifetime.
      // - Override this bit for GuardReflect based on the decomp-backed reflect timer (mv.co.guard.x14),
      //   so reflect-active is a window (timer) and not "entire action == GuardReflect".
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (init x14=x2A4)
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0 (tick/expire; clears fp->reflecting)
      // - Otherwise leave it seed-carry-through (other reflect windows not modeled yet).
      const uint8_t override_shine = (action_is_shine(batch->state.char_id[idx], a) ||
                                      action_is_shine(batch->state.char_id[idx], prev_a))
                                         ? 1u
                                         : 0u;
      const uint8_t override_guard_reflect =
          ((a == (uint16_t)MSL_ACT_GUARD_REFLECT) || (prev_a == (uint16_t)MSL_ACT_GUARD_REFLECT))
              ? 1u
              : 0u;
      if (zelda_nayru_active != 0u) {
        // Zelda Nayru's Love creates a ReflectDesc from cmd_var0's script window, not from a
        // looping state family. ftColl_CreateReflectHit copies ReflectDesc.x20_behavior into
        // fp+0x2218_b5.
        // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialN.c::{
        //   ftZd_SpecialN_Anim,ftZd_SpecialAirN_Anim}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
        if (ch->zelda_nayru_reflector_behavior != 0) {
          f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
        } else {
          f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
        }
        batch->state.state_flags[flags_i] = f;
      } else if (cid == (uint8_t)MSL_CHAR_ID_ZELDA && action_is_zelda_nayru(a)) {
        // ftZd_SpecialN_Anim clears fp->reflecting when the script cmd0 window is not active.
        // The reflect bit is therefore owned for the full Nayru action, not just while the bubble
        // exists, and stale replay seed carry must be cleared after cmd0 returns to zero.
        // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialN.c::{
        //   ftZd_SpecialN_Anim,ftZd_SpecialAirN_Anim}
        f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
        f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
        batch->state.state_flags[flags_i] = f;
      } else if (override_shine) {
        // SpecialLwStart is normally not reflect-active, but ftFx_SpecialLwStart_Pass creates a
        // ReflectDesc after changing to SpecialAirLwStart. Carry that already-live fp->reflecting
        // bit until the Start anim reaches Loop, without admitting fresh airborne Start entries.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Pass
        const uint8_t bubble_fx_kind =
            msl_motion_state_fx_special_kind(batch->state.char_id[idx], a);
        const uint8_t shine_start_pass_reflecting =
            ((bubble_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_LW_START ||
              bubble_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) &&
             (f & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) != 0u)
                ? 1u
                : 0u;
        const uint8_t want =
            (uint8_t)((action_is_shine_reflector_active(batch->state.char_id[idx], a) != 0) ||
                      (shine_start_pass_reflecting != 0u));
        if (want) {
          f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
          // Decomp: ftColl_CreateReflectHit writes ReflectDesc.x20_behavior into fp->x2218_b5.
          // Shine loop/hit/turn states recreate this lane from Fox/Falco special attrs each time
          // reflector creation is active, so state_flags[0] bit0x04 must follow reflector_behavior.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_CreateReflectHit
          // data/characters/{fox,falco}.json::reflector_behavior
          if (ch->reflector_behavior != 0) {
            f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
          } else {
            f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
          }
        } else {
          f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
          const uint8_t bubble_prev_kind =
              msl_motion_state_fx_special_kind(batch->state.char_id[idx], prev_a);
          if ((bubble_prev_kind == (uint8_t)MSL_FX_KIND_SPECIAL_LW_START ||
               bubble_prev_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) &&
              batch->state.action_frame[idx] == 0) {
            // Reflector-start direct exit reset:
            // - SpecialLwStart / SpecialAirLwStart own the transient ReflectDesc.x20_behavior lane
            //   while the startup reflector bubble is active,
            // - direct startup exits into common destinations go through Fighter_ChangeMotionState
            //   without a loop/hit/turn reflector callback to re-own that lane,
            // - clear the stale fp->x2218_b5 carry on the destination entry snapshot only for this
            //   start-action exit shape.
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
            //   ftFx_SpecialLwStart_Anim,ftFx_SpecialAirLwStart_Anim}
            f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
          }
        }
        batch->state.state_flags[flags_i] = f;
      } else if (override_guard_reflect) {
        // Drive Slippi reflect-active (fp+0x2218 bit4 => state_flags[0] bit 0x10) from the
        // decomp-shaped GuardReflect timer stored in batch state.
        //
        // Bit packing reference:
        // - refs/melee/src/melee/ft/types.h (fp+0x2218 bitfield includes `reflecting`)
        // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (packs fp+0x2218 into state_flags[0])
        const uint8_t want = (batch->state.guard_reflect_timer_x14[idx] > 0) ? 1u : 0u;
        if (want) {
          f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
          if (batch->state.action_frame[idx] <= 0) {
            // GuardReflect entry builds ReflectDesc with x20_behavior=1 and
            // ftColl_CreateReflectHit copies that into fp->x2218_b5.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
            f |= (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR;
          }
        } else {
          f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
        }
        batch->state.state_flags[flags_i] = f;
      }

      // Publish one complete live ReflectDesc after its callback-owned identity bit is settled.
      // Geometry consumes the persistent collision JObj pose, including SkipAnim and dynamic-node
      // state, just as lbColl_80007BCC consumes reflect_hit.bone. Descriptor parameters stay beside
      // the sphere so item families never infer them from character/action ids.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
      batch->state.reflector_x[idx] = batch->state.pos_x[idx];
      batch->state.reflector_y[idx] = batch->state.pos_y[idx];
      batch->state.reflector_z[idx] = batch->state.pos_z[idx];
      batch->state.reflector_radius[idx] = 0.0f;
      batch->state.reflector_damage_mul[idx] = 1.0f;
      batch->state.reflector_speed_mul[idx] = 1.0f;
      batch->state.reflector_max_damage[idx] = 0;

      if ((f & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u || ch == NULL) {
        continue;
      }

      uint16_t part_id = UINT16_MAX;
      float offset[3] = {0.0f, 0.0f, 0.0f};
      float size = 0.0f;
      float guard_joint_scale = 1.0f;
      if (zelda_nayru_active != 0u) {
        part_id = ch->zelda_nayru_reflector_bone_part_id;
        offset[0] = ch->zelda_nayru_reflector_offset_x;
        offset[1] = ch->zelda_nayru_reflector_offset_y;
        offset[2] = ch->zelda_nayru_reflector_offset_z;
        size = ch->zelda_nayru_reflector_size;
        batch->state.reflector_damage_mul[idx] = ch->zelda_nayru_reflector_damage_mul;
        batch->state.reflector_speed_mul[idx] = ch->zelda_nayru_reflector_speed_mul;
        batch->state.reflector_max_damage[idx] = ch->zelda_nayru_reflector_max_damage;
      } else if (is_fox_falco(cid) && action_is_shine(cid, a)) {
        part_id = ch->reflector_bone_part_id;
        offset[0] = ch->reflector_offset_x;
        offset[1] = ch->reflector_offset_y;
        offset[2] = ch->reflector_offset_z;
        size = ch->reflector_size;
        batch->state.reflector_damage_mul[idx] = ch->reflector_damage_mul;
        batch->state.reflector_speed_mul[idx] = ch->reflector_speed_mul;
        batch->state.reflector_max_damage[idx] = ch->reflector_max_damage;
      } else if (action_is_guard_reflector_owner(a) != 0u) {
        const MslCommonParams* common = msl_common_params();
        if (common == NULL) {
          continue;
        }
        part_id = msl_shield_part_id(cid);
        size = common->powershield_reflect_size;
        batch->state.reflector_damage_mul[idx] = common->powershield_reflect_damage_mul;
        batch->state.reflector_speed_mul[idx] = common->powershield_reflect_speed_mul;
        batch->state.reflector_max_damage[idx] = (int32_t)batch->state.shield_hp[idx];
        if (common->start_shield_health > 0.0f && ch->initial_shield_size > 0.0f) {
          float light = batch->state.lightshield_amount[idx];
          if (light < 0.0f) {
            light = 0.0f;
          } else if (light > 1.0f) {
            light = 1.0f;
          }
          float hp = batch->state.shield_hp[idx] / common->start_shield_health;
          if (hp < 0.0f) {
            hp = 0.0f;
          } else if (hp > 1.0f) {
            hp = 1.0f;
          }
          const float light_scale =
              light * (common->shield_size_lightshield_max - common->shield_size_lightshield_min) +
              common->shield_size_lightshield_min;
          guard_joint_scale = ((1.0f - common->shield_size_min_scale) * hp * light_scale +
                               common->shield_size_min_scale) *
                              ch->initial_shield_size;
        }
      } else {
        // The serialized `reflecting` bit proves that a ReflectDesc existed, but it does not carry
        // the descriptor's bone, offset, size, or callback. Only source-known supported owners may
        // publish geometry; an unrelated seeded action must fail closed instead of borrowing the
        // common Guard descriptor.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        continue;
      }

      MslFighterCollisionPose pose;
      const float axis_offset[3] = {offset[0] + 1.0f, offset[1], offset[2]};
      float center[3];
      float axis[3];
      if (part_id == UINT16_MAX || !(size > 0.0f) ||
          fighter_pose_collision_pose(batch, idx, &pose) == 0u ||
          fighter_pose_attachment_pair_local(batch, idx, pose.msid, pose.anim_frame, part_id,
                                             offset, axis_offset, center, axis, NULL) != 0) {
        continue;
      }
      const float facing = pose.facing;
      batch->state.reflector_x[idx] = batch->state.pos_x[idx] + facing * center[2];
      batch->state.reflector_y[idx] = batch->state.pos_y[idx] + center[1];
      batch->state.reflector_z[idx] = batch->state.pos_z[idx] - facing * center[0];
      const float dx = axis[0] - center[0];
      const float dy = axis[1] - center[1];
      const float dz = axis[2] - center[2];
      const float radius = size * guard_joint_scale * sqrtf(dx * dx + dy * dy + dz * dz);
      batch->state.reflector_radius[idx] = (isfinite(radius) && radius > 0.0f) ? radius : 0.0f;
    }
  }
}
