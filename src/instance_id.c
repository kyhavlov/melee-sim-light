#include "instance_id.h"

#include <stdint.h>

#include "attack_id_tables.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

// Fox/Falco motion states (GALE01) for SpecialN.
//
// Decomp (explicit numeric ids in comments):
// - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
//   (ftFx_MS_SpecialNStart=341 .. ftFx_MS_SpecialAirNEnd=346)
// - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
enum {
  MSL_ACT_FX_SPECIAL_N_START = 0x0155,      // ftFx_MS_SpecialNStart
  MSL_ACT_FX_SPECIAL_N_LOOP = 0x0156,       // ftFx_MS_SpecialNLoop
  MSL_ACT_FX_SPECIAL_N_END = 0x0157,        // ftFx_MS_SpecialNEnd
  MSL_ACT_FX_SPECIAL_AIR_N_START = 0x0158,  // ftFx_MS_SpecialAirNStart
  MSL_ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159,   // ftFx_MS_SpecialAirNLoop
  MSL_ACT_FX_SPECIAL_AIR_N_END = 0x015A,    // ftFx_MS_SpecialAirNEnd
  // Common AttackLw3 (dtilt) action id is ftCo_MS_AttackLw3 = 57 (0x0039).
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  MSL_ACT_CO_ATTACK_LW3 = 0x0039,  // ftCo_MS_AttackLw3
};

static inline uint8_t is_fox_falco(uint8_t char_id) {
  return (char_id == (uint8_t)MSL_CHAR_FOX) || (char_id == (uint8_t)MSL_CHAR_FALCO);
}

static inline uint8_t action_is_blaster(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_N_START:
    case MSL_ACT_FX_SPECIAL_N_LOOP:
    case MSL_ACT_FX_SPECIAL_N_END:
    case MSL_ACT_FX_SPECIAL_AIR_N_START:
    case MSL_ACT_FX_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_N_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_calls_ft_80089824_on_change_action(uint16_t action_id) {
  // Decomp callsites (GALE01):
  // - ftCo_AttackLw3 sets fp->x21EC callback that calls ft_80089824 on motion-state change.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::callUnk
  // - Fox/Falco SpecialN OnChangeAction calls ft_80089824 directly.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction
  if (action_id == (uint16_t)MSL_ACT_CO_ATTACK_LW3) {
    return 1;
  }
  if (action_is_blaster(action_id)) {
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

  // Decomp call semantics:
  // - Fighter_ChangeMotionState calls ft_800895E0(fp, new_motion_state->x4_flags) once on motion-state
  //   entry (i.e., a true action transition).
  //   refs/melee/src/melee/ft/fighter.c (ChangeMotionState path)
  //   refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  //
  // Simulator wiring:
  // - We hook the update to msl_anim_timebase_enter() (our common "enter action" helper), but some
  //   codepaths can reset the animation timebase without changing action_id.
  // - Guard against those "anim restarts" so we don't incorrectly bump fp->x2088.
  if (batch->state.instance_identity_last_action_id[idx] == action_id) {
    return;
  }
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
  if (is_fox_falco(char_id) && action_calls_ft_80089824_on_change_action(action_id)) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    batch->state.instance_id[idx] = inc_instance_id_plAttack_80037B08(batch, bi);
  }
}
