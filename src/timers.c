#include "timers.h"

#include <stddef.h>
#include <stdint.h>

void timers_update(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // Dataset packing/layout reference:
  // tools/slippi/make_dataset_from_slp.py (stack order 0..4 into `state_flags[..., 5]`)
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };

  // Decomp-first references (GALE01):
  //
  // Hitlag:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //   decrements `fp->dmg.x195c_hitlag_frames` by 1.0f each frame and clamps at 0.0f.
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //   clears the per-fighter hitlag flag `fp->x221A_b2 = 0` when hitlag reaches 0.
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //   sets `fp->x221A_b2 = 1` when `fp->dmg.x195c_hitlag_frames > 0.0f`.
  // - refs/melee/src/melee/ft/types.h
  //   declares `x221A_b2` as a 1-bit field at fp+0x221A (the byte Slippi records into `state_flags[...,1]`).
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //   records hitlag frames left from offset 0x195c (`lwz r3,0x195c(REG_PlayerData)`).
  //
  // Hitstun frames left (as exposed by Slippi) live in the action-state motion var:
  // - refs/melee/src/melee/ft/chara/ftCommon/types.h::union ftCommon_MotionVars::damage.x0
  //   is at offset 0x2340.
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //   records "misc AS variable" from offset 0x2340 (`lwz r3,0x2340(REG_PlayerData)`),
  //   interpreted as hitstun frames left when the hitstun flag is set.
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  //   decrements `fp->mv.co.damage.x0` by 1 each call; it is invoked from
  //   `ftCo_Damage_Anim`, which is called via `anim_cb` only when not in hitlag
  //   (refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 has an `if (!fp->x2219_b5)` gate).
  //
  // Practical sim rule: always decrement hitlag; only decrement hitstun when hitlag is 0.
  // Additionally, Slippi `misc_as` can mean other things when not in hitstun; gate hitstun
  // decrement on the "isHitstun" flag:
  // - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm comment:
  //   `lbz r3,0x221C(REG_PlayerData)   #0x2 = isHitstun`

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      uint16_t hl = batch->state.hitlag[idx];
      if (hl > 0) {
        hl--;
        batch->state.hitlag[idx] = hl;
      }

      // Keep the Slippi `state_flags` "isHitlag" bit consistent with `hitlag` frames left.
      //
      // Decomp: `fp->x221A_b2` is toggled by the engine with hitlag start/end:
      // - set to 1 when hitlag is active (Fighter_ProcessHit_8006D1EC),
      // - cleared to 0 when hitlag reaches 0 (Fighter_8006A1BC).
      // refs/melee/src/melee/ft/fighter.c
      //
      // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
      // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
      const size_t flags_221a_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
      uint8_t flags_221a = batch->state.state_flags[flags_221a_i];
      if (hl > 0) {
        flags_221a |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
      } else {
        flags_221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
      }
      batch->state.state_flags[flags_221a_i] = flags_221a;

      const uint8_t flags_221c =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t is_hitstun = (flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1 : 0;
      if (hl == 0 && is_hitstun) {
        uint16_t hs = batch->state.hitstun[idx];
        if (hs > 0) {
          hs--;
          batch->state.hitstun[idx] = hs;
        }

        // Decomp: hitstun flag is cleared when the hitstun timer reaches 0.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
        if (hs == 0) {
          const size_t flags_221c_i =
              idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
          batch->state.state_flags[flags_221c_i] &=
              (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
        }
      }
    }
  }
}
