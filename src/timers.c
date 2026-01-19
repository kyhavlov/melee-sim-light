#include "timers.h"

#include <stddef.h>
#include <stdint.h>

void timers_update(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // Dataset packing/layout reference:
  // tools/slippi/make_dataset_from_slp.py (stack order 0..4 into `state_flags[..., 5]`)
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };

  // Decomp-first references (GALE01):
  //
  // Hitlag:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //   decrements `fp->dmg.x195c_hitlag_frames` by 1.0f each frame and clamps at 0.0f.
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

      const uint8_t flags_221c =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t is_hitstun = (flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1 : 0;
      if (hl == 0 && is_hitstun) {
        uint16_t hs = batch->state.hitstun[idx];
        if (hs > 0) {
          hs--;
          batch->state.hitstun[idx] = hs;
        }
      }
    }
  }
}
