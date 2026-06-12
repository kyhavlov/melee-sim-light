#include "reflector_bubbles.h"
#include "motion_state_owners.h"
#include "char_registry.h"
#include "ids.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "char_params.h"
#include "mtx34.h"

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_ID_FOX) || (char_id == (uint8_t)MSL_CHAR_ID_FALCO);
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

void reflector_bubbles_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      if (!is_fox_falco(cid)) {
        batch->state.reflector_x[idx] = batch->state.pos_x[idx];
        batch->state.reflector_y[idx] = batch->state.pos_y[idx];
        batch->state.reflector_radius[idx] = 0.0f;
        continue;
      }

      const MslCharParams* ch = msl_char_params_fast(cid);
      if (ch == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t active = action_is_shine_reflector_active(batch->state.char_id[idx], a);

      float rx = batch->state.pos_x[idx];
      float ry = batch->state.pos_y[idx];
      float rr = 0.0f;

      if (active) {
        // Bubble size/attachment comes from ReflectDesc in the character special attrs section:
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit (reflect_hit.bone/offset/size)
        const float scale_y = batch->state.fighter_scale_y[idx];
        const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
        rr = ch->reflector_size * scale_y;

        const uint32_t anim_u32 = batch->state.animation_index[idx];
        if (anim_u32 <= 0xFFFFu) {
          const uint16_t msid = (uint16_t)anim_u32;
          const float anim_frame_f32 =
              msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
          const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);

          float m[12];
          if (anim_pose_get_matrix(cid, msid, frame, ch->reflector_bone_part_id, m) == 0) {
            float off[3] = {ch->reflector_offset_x, ch->reflector_offset_y, ch->reflector_offset_z};
            float lx = 0.0f, ly = 0.0f, lz = 0.0f;
            msl_mtx34_mul_point(m, off, &lx, &ly, &lz);
            (void)lz;

            lx *= (scale_y * facing_dir);
            ly *= scale_y;

            rx = batch->state.pos_x[idx] + lx;
            ry = batch->state.pos_y[idx] + ly;
          }
        }
      }

      batch->state.reflector_x[idx] = rx;
      batch->state.reflector_y[idx] = ry;
      batch->state.reflector_radius[idx] = (isfinite(rr) && rr > 0.0f) ? rr : 0.0f;

      const size_t flags_i =
          idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      uint8_t f = batch->state.state_flags[flags_i];

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
      const uint16_t prev_a = batch->state.prev_action_id[idx];
      const uint8_t override_shine = (action_is_shine(batch->state.char_id[idx], a) ||
                                      action_is_shine(batch->state.char_id[idx], prev_a))
                                         ? 1u
                                         : 0u;
      const uint8_t override_guard_reflect =
          ((a == (uint16_t)MSL_ACT_GUARD_REFLECT) || (prev_a == (uint16_t)MSL_ACT_GUARD_REFLECT))
              ? 1u
              : 0u;
      if (override_shine) {
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
    }
  }
}
