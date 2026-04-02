#include "state_flags.h"

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "common_params.h"
#include "move_tables.h"
#include "state_flags_221c_y_tables.h"

static inline uint8_t state_flags_221a_b7_action_uses_guard_shield(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t state_flags_2218_allow_interrupt_attackair_action(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_F:
    case MSL_ACT_ATTACK_AIR_B:
    case MSL_ACT_ATTACK_AIR_HI:
    case MSL_ACT_ATTACK_AIR_LW:
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

void state_flags_refresh_post_frame(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_2218_INDEX = 0 };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  enum { MSL_STATE_FLAGS_221B_INDEX = 2 };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAGS_221F_INDEX = 4 };
  enum { MSL_STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80 };
  enum { MSL_STATE_FLAG_2218_B1 = 0x40 };
  enum { MSL_STATE_FLAG_2218_B2 = 0x20 };

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
  enum { MSL_STATE_FLAG_221A_IS_FASTFALL = 0x08 };
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };
  enum { MSL_STATE_FLAG_221A_B5 = 0x04 };
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };
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
  // Movescript opcode 52 writes fp->x221C_u16_y (3-bit field). Slippi emits only fp+0x221C
  // (the high byte at +0x221C), and the overlapping exported lane there is high-byte bit0
  // (mask 0x01).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80072C6C
  // refs/melee/src/melee/ft/ft_0892.c::ft_8008A1B8
  // refs/melee/src/melee/ft/types.h (fp+0x221C bitfield layout)
  enum { MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT = 0x01 };

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
  enum { MSL_STATE_FLAG_221F_B1 = 0x40 };

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
      const size_t flags_2218_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_2218_INDEX;
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
      } else if (action_id == (uint16_t)MSL_ACT_ESCAPE_N) {
        // EscapeN (spotdodge) `allow_interrupt` lane:
        // - ftAction command script emits `allow_interrupt` (ftAction_80071950) during EscapeN.
        // - Slippi state_flags[0] bit 0x80 mirrors fp->allow_interrupt (fp+0x2218 bit0).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        // data/moves/{fox,falco}.json moves["ftCo_SM_EscapeN"]["events"]
        // EscapeN script timing is integer-framed in-suite; use action_frame to avoid float-timebase
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
        const uint8_t jab_combo_active =
            move_tables_jab_combo_active(batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
        const uint8_t jab_rapid_active =
            move_tables_jab_rapid_active(batch->state.char_id[idx], action_id, allow_interrupt_anim_probe);
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
      // AttackDash exit ownership bridge for fp+0x2218 bit0 (0x80):
      // - ftCo_AttackDash_IASA runs ftCo_800D8AE0, then delegates to Wait_IASA family checks.
      // - Those checks can consume into Squat/GuardOn on the same frame.
      // - In decomp, fp+0x2218 is command-owned (ftAction_80071950); replay rows in these
      //   transitions expose bit0x80 set on the destination frame, while this simulator does not
      //   yet run full command ownership for non-Attack* destinations.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
      // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
      const uint16_t prev_action_2218 = batch->state.prev_action_id[idx];
      if ((state_flags_2218_allow_interrupt_grounded_attack_action(prev_action_2218) &&
           action_id == (uint16_t)MSL_ACT_SQUAT && action_frame_i <= 1) ||
          (prev_action_2218 == (uint16_t)MSL_ACT_ATTACK_DASH &&
           action_id == (uint16_t)MSL_ACT_GUARD_ON && action_frame_i <= 0)) {
        f2218 |= (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT;
      }
      batch->state.state_flags[flags_2218_i] = f2218;

      // 0x221A: HasIntangOrInvinc + isFastFalling.
      const size_t flags_221a_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
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
           state_flags_221a_b7_action_uses_guard_shield(batch->state.action_id[idx]))
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
      const size_t flags_221c_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      uint8_t f221c = batch->state.state_flags[flags_221c_i];
      if (batch->state.hitstun[idx] > 0) {
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
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 <= 0xFFFFu) {
        const uint16_t msid = (uint16_t)anim_u32;
        const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
        uint8_t y_flags = 0u;
        if (state_flags_221c_y_get(batch->state.char_id[idx], msid, frame, &y_flags) == 0) {
          if (y_flags & 0x4u) {
            f221c |= (uint8_t)MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT;
          } else {
            f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT;
          }
        } else {
          // No opcode-52 timeline for this (char, msid): keep x221C_u16_y visible bit cleared to
          // avoid stale seeded carryover into states that do not script-drive this lane.
          f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT;
        }
      } else {
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT;
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
        f221c &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_U16_Y_VISIBLE_BIT;
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
      }
      batch->state.state_flags[flags_221c_i] = f221c;

      // 0x221F_b1 transition ownership:
      // - KO start path sets fp->x221F_b1 = 1 (dead flow setup).
      // - Entry setup sets fp->x221F_b1 = 1 and Entry -> EntryStart keeps it for the intro flow.
      // - Generic motion-state reset clears fp->x221F_b1 on Dead{Down,Left,Right} -> Rebirth.
      // refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3680
      // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C61B0
      // refs/melee/src/melee/ft/fighter.c (motion-state reset clears fp->x221F_b1)
      const size_t flags_221f_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221F_INDEX;
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
      if (action_id == (uint16_t)MSL_ACT_ENTRY_START) {
        // EntryStart snapshots follow Fighter_ChangeMotionState reset ownership; do not carry
        // seeded dead-flow x221F_b1 into this transition destination.
        // refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C6408
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        f221f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      if (action_id == (uint16_t)MSL_ACT_REBIRTH) {
        // Generic motion-state reset owns fp->x221F_b1 clear on Rebirth entry.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dead.c
        (void)prev_action;
        f221f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221F_B1;
      }
      batch->state.state_flags[flags_221f_i] = f221f;
    }
  }
}
