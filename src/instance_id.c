#include "instance_id.h"
#include "motion_state_owners.h"
#include "ids.h"

#include <stdint.h>

#include "action_ids.h"
#include "attack_id_tables.h"

enum {
  // Common AttackLw3 (dtilt) action id is ftCo_MS_AttackLw3 = 57 (0x0039).
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  MSL_ACT_CO_ATTACK_LW3 = 0x0039,  // ftCo_MS_AttackLw3
};

static inline uint8_t action_is_blaster_loop(uint8_t char_id, uint16_t action_id) {
  // Ownership from the extracted MotionState row identity (the Blaster loop rows);
  // ftMs_SpecialNLoop (Marth, same numeric ids) stays kind 0.
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_N_LOOP ||
          fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP)
             ? 1
             : 0;
}

static inline uint8_t motion_state_change_calls_ft_80089824(uint8_t char_id,
                                                            uint16_t prev_action_id,
                                                            uint16_t next_action_id) {
  // Decomp callsites (GALE01):
  // - ftCo_AttackLw3 sets fp->x21EC callback that calls ft_80089824 on AttackLw3 entry.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::callUnk
  // - Fox/Falco SpecialN OnChangeAction calls ft_80089824 directly.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction
  //
  // AttackLw3:
  // - doEnter() installs fp->x21EC=callUnk before Fighter_ChangeMotionState(AttackLw3).
  // - fighter.c calls x21EC after ft_800895E0 inside that same Fighter_ChangeMotionState bundle.
  // - callUnk() calls ft_800892A0 and ft_80089824, so the extra x2088 writer is tied to the
  //   AttackLw3 entry, not to the later state being left.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::doEnter
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::callUnk
  // refs/melee/src/melee/ft/fighter.c (x21EC call after ft_800895E0)
  //
  // Special{Air}NLoop loop-restart:
  // - The *_Loop_Anim() callbacks set fp->x21EC=ftFx_SpecialN_OnChangeAction only on the loop
  //   restart path (motion state change from Loop -> Loop).
  // - The Start -> Loop transition does *not* install x21EC, so it must not trigger ft_80089824.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
  if (next_action_id == (uint16_t)MSL_ACT_CO_ATTACK_LW3) {
    return 1;
  }
  if (action_is_blaster_loop(char_id, prev_action_id) && prev_action_id == next_action_id) {
    return 1;
  }
  return 0;
}

static inline uint16_t inc_instance_id_plAttack_80037B08(MslBatch* batch, int bi) {
  // Decomp: plAttack_80037B08 never returns 0; it wraps and skips 0.
  // refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
  if (batch == NULL) {
    return 0;
  }
  uint16_t before = batch->state.instance_id_counter[bi];
  if (before == 0) {
    before = 1;
  }
  uint16_t after = (uint16_t)(before + 1u);
  if (after == 0) {
    after = 1;
  }
  batch->state.instance_id_counter[bi] = after;
  return before;
}

static inline void lower_bound_instance_id_counter(MslBatch* batch, int bi, uint16_t used_id) {
  if (batch == NULL || used_id == 0u) {
    return;
  }
  uint16_t next = (uint16_t)(used_id + 1u);
  if (next == 0u) {
    next = 1u;
  }
  const uint16_t current = batch->state.instance_id_counter[bi];
  // Suite seed ranges are far from wrap; keep this as a monotonic lower bound for the replay-facing
  // same-frame order lane instead of trying to solve wrap ordering from post-frame snapshots.
  if (current == 0u || current < next) {
    batch->state.instance_id_counter[bi] = next;
  }
}

void instance_id_counter_consume_plAttack_80037B08(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  (void)inc_instance_id_plAttack_80037B08(batch, bi);
}

void instance_id_reset_ft_800892D4(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp/asm: ft_800892D4 clears fp->x2088 (halfword store of 0).
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s:106  /* 80089454 ... */ sth r0, 0x2088(r3)
  batch->state.instance_id[idx] = 0;
  // Subset: also clear the byte ft_800895E0 compares against (fp+0x2073) and reset our guard.
  batch->state.instance_id_x2073[idx] = 0;
  batch->state.instance_identity_last_action_id[idx] = 0xFFFFu;
}

void instance_id_on_motion_state_change_ft_800895E0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  const uint16_t action_id = batch->state.action_id[idx];
  // Decomp shape:
  // - ft_800895E0 / ft_80089824 are evaluated per Fighter_ChangeMotionState invocation.
  // - Multiple motion-state entries can occur in one simulated frame (Anim/IASA chaining), so the
  //   "action being left" for ft_80089824 gating must track the most recent entered action within
  //   the frame, not only the frame-start cached prev_action_id.
  //
  // Simulator mapping:
  // - `instance_identity_last_action_id` is seeded from seed action_id and updated on every
  //   msl_anim_timebase_enter() identity bundle.
  // - Use it as the primary "previous action" source; fall back to frame-start prev_action_id if
  //   the lane is uninitialized.
  const uint16_t prev_action_id_cached = batch->state.prev_action_id[idx];
  const uint16_t prev_action_id_last = batch->state.instance_identity_last_action_id[idx];
  const uint16_t prev_action_id =
      (prev_action_id_last != 0xFFFFu) ? prev_action_id_last : prev_action_id_cached;

  // Decomp call semantics:
  // - Fighter_ChangeMotionState calls ft_800895E0(fp, new_motion_state->x4_flags) once on motion-state
  //   entry (i.e., a true action transition).
  //   refs/melee/src/melee/ft/fighter.c (ChangeMotionState path)
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  //
  // Simulator wiring:
  // - This is wired to msl_anim_timebase_enter() (the decomp-shaped Fighter_ChangeMotionState bundle).
  // - Pure animation timebase restarts should use msl_anim_timebase_restart() so they do not call
  //   ft_800895E0 / ft_80089824.
  batch->state.instance_identity_last_action_id[idx] = action_id;

  const uint8_t char_id = batch->state.char_id[idx];
  const uint32_t x4_flags = attack_id_x4_flags_from_action(char_id, action_id);

  // ft_800895E0 stores the 32-bit flags argument to the stack and reads the low byte via lbz,
  // then compares it against fp+0x2073 (a byte within fp->x2070).
  // If the byte is 0 OR differs from fp+0x2073, it bumps fp->x2088 via plAttack_80037B08.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz + compare + plAttack call)
  //
  // NOTE(unmodeled-ft_800895E0-rewrite):
  // ft_800895E0 rewrites the x2070 word (which contains fp+0x2073) in two cases:
  // - If fp->kind == 0x11 and flags_low == 0x71, it overwrites the word with 0x240063.
  // - If flags_low == 0x62 and it_8026B6C8(fp->x1974) is true, it overwrites the word with 0x44003D.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  //
  // The simulator currently does not model fighter-kind/item-pointer state for this rewrite, so it
  // treats fp+0x2073 as (u8)x4_flags. A guard test enforces that suite-observed action_ids never
  // use x4_flags low bytes 0x71 or 0x62 for Fox/Falco; if they do, we must implement the rewrite
  // logic or add the required seeded state first.
  const uint8_t flags_low = (uint8_t)(x4_flags & 0xFFu);
  const uint16_t entry_override = batch->state.motion_entry_instance_id_override[idx];
  if (entry_override != 0u) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    batch->state.instance_id[idx] = entry_override;
    batch->state.instance_id_x2073[idx] = flags_low;
    lower_bound_instance_id_counter(batch, bi, entry_override);
    return;
  }

  const uint8_t prev_x2073 = batch->state.instance_id_x2073[idx];
  if (flags_low == 0 || flags_low != prev_x2073) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    batch->state.instance_id[idx] = inc_instance_id_plAttack_80037B08(batch, bi);
  }
  // Subset: maintain the byte that the next ft_800895E0 invocation compares against.
  batch->state.instance_id_x2073[idx] = flags_low;

  // Additional writer: ft_80089824 bumps fp->x2088 unconditionally. It is invoked by certain
  // OnChangeAction hooks (x21EC callback) on motion-state change.
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::callUnk
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824 (plAttack_80037B08; sth ..., 0x2088)
  // No char-family gate: ftCo_AttackLw3's callUnk -> ft_80089824 is COMMON ftCo code (every
  // character's dtilt entry bumps fp->x2088); the blaster-loop branch inside the helper is
  // already keyed on the extracted MotionState kind. The old fox/falco gate silently kept
  // other characters' dtilt from bumping the instance counter (persistent divergence).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::{doEnter,callUnk}
  if (motion_state_change_calls_ft_80089824(batch->state.char_id[idx], prev_action_id, action_id)) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    batch->state.instance_id[idx] = inc_instance_id_plAttack_80037B08(batch, bi);
  }
}
