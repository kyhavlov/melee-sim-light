#include "state_flags.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "common_params.h"

void state_flags_refresh_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  enum { MSL_STATE_FLAGS_221B_INDEX = 2 };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };

  // fp+0x221A:
  // - 0x01 = fp->x221A_b7
  // - 0x08 = isFastFalling
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221A bitfields)
  //
  // Decomp: fp->x221A_b7 is toggled alongside shield activation:
  // - set on GuardOn entry after ftColl_8007B1B8 (ftCo_80092450),
  // - cleared on GuardReflect entry (ftCo_8009388C) and shield break (ftCo_800925A4).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_8009388C,ftCo_800925A4}
  enum { MSL_STATE_FLAG_221A_IS_FASTFALL = 0x08 };
  enum { MSL_STATE_FLAG_221A_B7 = 0x01 };

  // fp+0x221B:
  // - 0x80 = isShieldActive (fp->x221B_b0 in the decomp bitfield layout)
  // - 0x04 = fp->x221B_b5 (grab-owner latch; set while this fighter owns a grabbed victim)
  // Bit-order note: fp+0x221B b* numbering is MSB-first in GALE01/Slippi packing
  // (b0==0x80 ... b5==0x04), matching refs/melee/src/melee/ft/types.h + SendGamePostFrame.asm.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221B bitfields)
  enum { MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE = 0x80 };
  enum { MSL_STATE_FLAG_221B_B5 = 0x04 };

  // fp+0x221C:
  // - 0x02 = isHitstun
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfields)
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };

  // fp+0x221C GuardReflect flags:
  // - x221C_b1 (mask 0x40) is cleared when mv.co.guard.x14 expires,
  // - x221C_b2 (mask 0x20) is "Powershield Active Bool" (Slippi post-frame) and is cleared when
  //   mv.co.guard.x18 expires,
  // - x221C_b3 (mask 0x10) is a 1-frame entry flag cleared on the next Anim tick.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (0x221C 0x20 = Powershield Active Bool)
  // Bitfield layout: refs/melee/src/melee/ft/types.h (fp+0x221C bits 0..3 map to masks 0x80..0x10).
  enum { MSL_STATE_FLAG_221C_B1 = 0x40 };
  enum { MSL_STATE_FLAG_221C_B2 = 0x20 };
  enum { MSL_STATE_FLAG_221C_B3 = 0x10 };

  // Seed/state timer representation uses a +1 bias; derive the entry value from ftCommonData.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c (mv.co.guard.x14 = p_ftCommonData->x2A4)
  const MslCommonParams* c = msl_common_params();
  uint8_t guard_reflect_timer_x14_init = 0;
  if (c != NULL) {
    uint16_t t = (uint16_t)c->powershield_reflect_frames;
    t = (uint16_t)(t + 1u);
    if (t > 255u) {
      t = 255u;
    }
    guard_reflect_timer_x14_init = (uint8_t)t;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // 0x221A: HasIntangOrInvinc + isFastFalling.
      const size_t flags_221a_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
      uint8_t f221a = batch->state.state_flags[flags_221a_i];
      if (batch->state.fall_fast[idx] != 0) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_IS_FASTFALL;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_FASTFALL;
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
      const size_t flags_221b_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221B_INDEX;
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

      // Approximate fp->x221A_b7 from shield activation (fp->x221B_b0) to keep the byte stable
      // under teacher-forced reseeds without introducing new hidden state.
      if (f221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) {
        f221a |= (uint8_t)MSL_STATE_FLAG_221A_B7;
      } else {
        f221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B7;
      }
      batch->state.state_flags[flags_221b_i] = f221b;
      batch->state.state_flags[flags_221a_i] = f221a;

      // 0x221C: isHitstun derived from hitstun frames left.
      const size_t flags_221c_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      uint8_t f221c = batch->state.state_flags[flags_221c_i];
      if (batch->state.hitstun[idx] > 0) {
        f221c |= (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
      } else {
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
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
      }
      batch->state.state_flags[flags_221c_i] = f221c;
    }
  }
}
