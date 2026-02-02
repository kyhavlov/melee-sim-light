#include "timers.h"

#include <stddef.h>
#include <stdint.h>

#include "common_params.h"

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
  // fp+0x221A bit 0x10 corresponds to x221A_b3 in decomp (see refs/melee/src/melee/ft/types.h).
  // Fighter_ProcessHit can set it alongside hitlag start, and Fighter_8006A1BC clears it on hitlag end.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };

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

  // Combo timer reset constant (GALE01 p_ftCommonData->x4CC).
  const MslCommonParams* c = msl_common_params();

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    // Per-batch pass: match decomp ordering inside Fighter_8006A360:
    // - ftColl_800764DC runs under `if (!fp->x2219_b5)` before the per-action `anim_cb`,
    // - hitstun decrement + `fp->x2098 = p_ftCommonData->x4CC` happens in ftCo_8008F744, which is
    //   called from the Damage state's anim callback (i.e., after ftColl_800764DC).
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    //
    // NOTE(hitlag_gate): GALE01 gates ftColl_800764DC on the hitlag callback flag `fp->x2219_b5`,
    // which is set/cleared by the hitlag recursive callback helpers:
    // - set: refs/melee/src/melee/ft/fighter.c::Fighter_UnkRecursiveFunc_8006D044 (`fp->x2219_b5 = 1`)
    // - clear: refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C (`fp->x2219_b5 = 0`)
    // In the light sim we don't represent `x2219_b5` directly; we approximate this gate using the
    // observable `hitlag` frames remaining (Slippi fp+0x195c) via `hl == 0`.

    // Pass 1: decrement hitlag + keep the Slippi "isHitlag" bit consistent with hitlag frames left.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      uint16_t hl = batch->state.hitlag[idx];
      // Decomp: hitlag frames are decremented at proc prio 0 before the main per-fighter update
      // block (Anim/Phys/Coll) runs.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
      if (hl > 0) {
        hl--;
        batch->state.hitlag[idx] = hl;
      }

      // Per-frame hitlag gate (see src/state.h for rationale).
      // Decomp update gate: refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (`if (!fp->x2219_b5)`).
      batch->state.hitlag_started_frame[idx] = (hl > 0) ? 1u : 0u;

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
        // Decomp: when hitlag ends, Fighter_8006A1BC clears x221A_b2 (isHitlag) and, if set,
        // clears x221A_b3 after calling ftCo_80090718(fp).
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
        flags_221a &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B3;
      }
      batch->state.state_flags[flags_221a_i] = flags_221a;
    }

    // Pass 2: combo timer tick + combo-victim clear (ftColl_800764DC family).
    //
    // IMPORTANT: run this pass before hitstun decrement/clear (ftCo_8008F744) so that a victim
    // whose hitstun reaches 0 this frame is still treated as "in hitstun" for the clear check,
    // matching Fighter_8006A360 call order.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t hl = batch->state.hitlag[idx];
      if (hl != 0) {
        continue;
      }

      // Decomp: ftColl_800764DC decrements `fp->x2098` if nonzero.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
      uint16_t t = batch->state.combo_timer_x2098[idx];
      if (t != 0) {
        t--;
        batch->state.combo_timer_x2098[idx] = t;
      }

      // Decomp: if `fp->x2094 != NULL`, clear it when the victim is not in hitstun and the
      // victim's `x2098` is 0.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
      const uint8_t v_port = batch->state.combo_victim_port[idx];
      if (v_port == 0xFFu) {
        continue;
      }
      if (v_port >= (uint8_t)num_players) {
        batch->state.combo_victim_port[idx] = 0xFFu;
        batch->state.combo_victim_instance_id[idx] = 0;
        continue;
      }

      // Decomp stores a raw victim GObj pointer in `fp->x2094` (no integer identity check).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
      //
      // We do not clear based on Slippi's `instance_id` (fp+0x2070 union-as-int), because it is
      // not a stable "fighter object identity" across frames; keep it only as a seeded overlay.
      // refs/melee/src/melee/ft/types.h (union Struct2070 at fp+0x2070, used as s32 x2070_int)
      const size_t v_idx = msl_idx_player(bi, (int)v_port);

      const uint8_t flags_221c =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      (void)flags_221c;
      const uint8_t v_flags_221c =
          batch->state.state_flags[v_idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t v_is_hitstun = (v_flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1 : 0;
      if (!v_is_hitstun && batch->state.combo_timer_x2098[v_idx] == 0) {
        batch->state.combo_victim_port[idx] = 0xFFu;
        batch->state.combo_victim_instance_id[idx] = 0;
      }
    }

    // Pass 3: hitstun decrement + hitstun end effects (ftCo_8008F744 family).
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t hl = batch->state.hitlag[idx];
      if (hl != 0) {
        continue;
      }

      const uint8_t flags_221c =
          batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + MSL_STATE_FLAGS_221C_INDEX];
      const uint8_t is_hitstun = (flags_221c & MSL_STATE_FLAG_221C_IS_HITSTUN) ? 1 : 0;
      if (is_hitstun) {
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

          // Decomp: when hitstun ends, set `fp->x2098 = p_ftCommonData->x4CC`.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
          if (c != NULL) {
            batch->state.combo_timer_x2098[idx] = c->combo_timer_post_hitstun_frames;
          } else {
            batch->state.combo_timer_x2098[idx] = 0;
          }
        }
      }
    }
  }
}
