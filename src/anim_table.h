#pragma once

#include <stdint.h>

// Init-time loader for per-animation end frames (FigaTree.frames) for a subset of submotions.
//
// Source of truth: `data/anims/<character>.tracks.bin` (ISO-derived).
// Extractor: `tools/extraction/extract_fighter_anims.py` (decomp-first).
//
// IMPORTANT: anim_table_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

int anim_table_init(void);

// Return end_frame for the given character + submotion id (anim_id).
// Returns 0.0f if unknown/unloaded.
float msl_anim_end_frame(uint8_t char_id, uint16_t submotion_id);

// Return 1 if the fighter animation is configured to drive TransN root motion (`fp->x594_b0`).
//
// Source of truth: `data/anims/<character>.tracks.bin` (ISO-derived), per-msid
// `uses_root_motion` extracted from `ftData_80085FD4_ret.x10_b0` in the ftData msid table.
// Decomp:
// - refs/melee/src/melee/ft/types.h::ftData_80085FD4_ret (+0x10 bit0)
// - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState (loads fp->x594_s32)
// - refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0} (root-motion branch)
uint8_t msl_anim_uses_root_motion(uint8_t char_id, uint16_t submotion_id);

// Return 1 if the fighter animation is configured to loop (AOBJ_LOOP).
//
// Source of truth: `data/anims/<character>.tracks.bin` (ISO-derived), per-msid `aobj_loop`
// extracted from `ftData_80085FD4_ret.x10_b1` (loop) in the ftData msid table.
// Decomp:
// - refs/melee/src/melee/ft/types.h::ftData_80085FD4_ret (+0x10 bit1)
// - refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8 (sets AOBJ_LOOP when fp->x594_b1_loop)
uint8_t msl_anim_is_looping(uint8_t char_id, uint16_t submotion_id);

// Return 1 if the given FtPart id is inside the extracted FtPart_XRotN subtree for this character.
//
// Source of truth: `data/anims/<character>.tracks.bin` header local_parts/local_parent derived from
// ISO fighter joints in `tools/extraction/extract_fighter_anims.py`.
uint8_t msl_anim_part_under_xrotn(uint8_t char_id, uint16_t part_id);
