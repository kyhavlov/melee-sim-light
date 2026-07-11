#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "anim_timebase.h"

enum {
  MSL_MOTION_ENTRY_KEEP_FASTFALL = 1u << 0,
  MSL_MOTION_ENTRY_SKIP_HIT = 1u << 3,
};

// Install the selected MotionState row's callback identities into mutable live fighter lanes.
// Map dispatch consumes these lanes directly. Explicit source callback replacements must use the
// override function rather than changing the selected row.
// refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
void motion_state_install_live_callbacks(MslBatch* batch, size_t idx);
void motion_state_finalize_seeded_coll_data_before_map(MslBatch* batch);

// Source callback-pointer assignment substrate. `callback_id` remains diagnostic identity;
// `handler_kind` is the stable runtime ABI generated from callback symbols.
// refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Escape.c,ftCo_ItemThrow.c}
void motion_state_override_live_coll_callback(MslBatch* batch, size_t idx, uint16_t callback_id,
                                              uint8_t handler_kind);

// Central source-shaped Fighter_ChangeMotionState subset used by Packet 1 destinations. The
// procedural flags are call-site arguments, not MotionState row x4 flags. Every migrated collision
// transition enters through this boundary so callbacks, HitCapsules, and timebase change causally
// together.
// refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
void motion_state_change(MslBatch* batch, int bi, int p, uint16_t action_id,
                         uint32_t animation_index, uint32_t flags, float anim_start,
                         float anim_speed, MslAnimEnterTickPolicy tick_policy);
