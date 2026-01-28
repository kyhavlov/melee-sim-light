#include "reflector_bubbles.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "char_params.h"
#include "mtx34.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

static inline uint8_t action_is_shine_reflector_active(uint16_t a) {
  // Suite-confirmed: Slippi reflect-active bit 0x10 is set in Loop/Turn but not Start/End.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwLoop_Enter (CreateReflectHit)
  switch (a) {
    case 0x0169:  // ftFx_MS_SpecialLwLoop
    case 0x016A:  // ftFx_MS_SpecialLwHit
    case 0x016C:  // ftFx_MS_SpecialLwTurn
    case 0x016E:  // ftFx_MS_SpecialAirLwLoop
    case 0x016F:  // ftFx_MS_SpecialAirLwHit
    case 0x0171:  // ftFx_MS_SpecialAirLwTurn
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_shine(uint16_t a) {
  // Decomp: refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  // (ftFx_MS_SpecialLwStart=360 .. ftFx_MS_SpecialAirLwTurn=369)
  switch (a) {
    case 0x0168:  // ftFx_MS_SpecialLwStart
    case 0x0169:  // ftFx_MS_SpecialLwLoop
    case 0x016A:  // ftFx_MS_SpecialLwHit
    case 0x016B:  // ftFx_MS_SpecialLwEnd
    case 0x016C:  // ftFx_MS_SpecialLwTurn
    case 0x016D:  // ftFx_MS_SpecialAirLwStart
    case 0x016E:  // ftFx_MS_SpecialAirLwLoop
    case 0x016F:  // ftFx_MS_SpecialAirLwHit
    case 0x0170:  // ftFx_MS_SpecialAirLwEnd
    case 0x0171:  // ftFx_MS_SpecialAirLwTurn
      return 1;
    default:
      return 0;
  }
}

void reflector_bubbles_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_2218_INDEX = 0 };
  enum { MSL_STATE_FLAG_2218_IS_REFLECT_ACTIVE = 0x10 };

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

      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }

      const uint16_t a = batch->state.action_id[idx];
      const uint8_t active = action_is_shine_reflector_active(a);

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
          const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
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

      const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      uint8_t f = batch->state.state_flags[flags_i];

      // Slippi state_flags byte0 bit0x10 mirrors fp->reflecting (GALE01 fp+0x2218 bit4).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit (sets fp->reflecting=true)
      //
      // Policy:
      // - Override this bit for Shine (SpecialLw) because we model the reflector bubble lifetime.
      // - Otherwise leave it seed-carry-through until GuardReflect/powershield reflect windows are modeled
      //   with explicit decomp-backed timers (ftCo_Guard.c::mv.co.guard.x14 / x2A4).
      const uint16_t prev_a = batch->state.prev_action_id[idx];
      const uint8_t override = (action_is_shine(a) || action_is_shine(prev_a)) ? 1u : 0u;
      if (override) {
        const uint8_t want = (action_is_shine_reflector_active(a) != 0) ? 1u : 0u;
        if (want) {
          f |= (uint8_t)MSL_STATE_FLAG_2218_IS_REFLECT_ACTIVE;
        } else {
          f &= (uint8_t)~(uint8_t)MSL_STATE_FLAG_2218_IS_REFLECT_ACTIVE;
        }
        batch->state.state_flags[flags_i] = f;
      }
    }
  }
}
