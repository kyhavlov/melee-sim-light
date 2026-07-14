#pragma once

#include <stddef.h>

#include "batch_internal.h"
#include "combat.h"

// Install a new motion's command stream. Fighter_ChangeMotionState immediately runs the source
// AObj interpreter: frame-zero entry uses ftAction_80073240 and nonzero entry seeks through
// ftAction_80073354.
// HitCapsule clearing/preservation is owned by the procedural Fighter_ChangeMotionState flags and
// must happen before this call.
// refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
void fighter_script_enter(MslBatch* batch, size_t idx);

// Apply explicit source entry-helper writes that occur immediately before
// Fighter_ChangeMotionState. Command fields not named by an entry helper remain persistent.
void fighter_script_prepare_motion_entry(MslBatch* batch, size_t idx);

// Initialize the same live state from a teacher-forced current-frame snapshot.
void fighter_script_reseed(MslBatch* batch, size_t idx);

// Advance every live command stream once after the AObj timebase and before the installed Anim
// callback. This mutates persistent command and HitCapsule state; it never rescans prior frames.
// refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
// refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
void fighter_script_advance(MslBatch* batch);
void fighter_script_advance_fighter(MslBatch* batch, int bi, int p);

// Advance one fighter's already-installed command stream for an explicit same-frame
// ftAnim_8006EBA4 call made outside the ordinary global animation pass.
void fighter_script_tick_once(MslBatch* batch, size_t idx);

static inline uint16_t fighter_script_cmd_var(const MslBatch* batch, size_t idx, uint8_t cmd_var) {
  if (batch == NULL || cmd_var >= 4u) {
    return 0u;
  }
  return batch->state.script_cmd_vars[idx * 4u + (size_t)cmd_var];
}

static inline void fighter_script_clear_cmd_var(MslBatch* batch, size_t idx, uint8_t cmd_var) {
  if (batch != NULL && cmd_var < 4u) {
    batch->state.script_cmd_vars[idx * 4u + (size_t)cmd_var] = 0u;
  }
}

static inline void fighter_script_set_cmd_var(MslBatch* batch, size_t idx, uint8_t cmd_var,
                                              uint16_t value) {
  if (batch != NULL && cmd_var < 4u) {
    batch->state.script_cmd_vars[idx * 4u + (size_t)cmd_var] = value;
  }
}

static inline uint8_t fighter_script_take_throw_flag(MslBatch* batch, size_t idx, uint8_t bit) {
  if (batch == NULL || bit >= 8u) {
    return 0u;
  }
  const uint8_t mask = (uint8_t)(1u << bit);
  const uint8_t set = (batch->state.script_throw_flags[idx] & mask) != 0u ? 1u : 0u;
  batch->state.script_throw_flags[idx] &= (uint8_t)~mask;
  return set;
}

// Query the installed command interpreter itself for timebase consumers. These helpers expose
// source CommandInfo state; they do not rescan the script at an absolute animation frame.
uint8_t fighter_script_command_due_next_tick(const MslBatch* batch, size_t idx);
uint8_t fighter_script_consumed_throw_release(const MslBatch* batch, size_t idx);

// Source ftColl_8007AFF8 path used by Fighter_ChangeMotionState when Ft_MF_SkipHit is absent.
// Disabling does not clear victim rings; a later create edge owns clear/copy.
// refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AFF8
void fighter_script_disable_hitcapsules(MslBatch* batch, size_t idx);

uint8_t fighter_script_throw_hitbox_params(const MslBatch* batch, size_t idx, uint8_t throw_id,
                                           MslThrowHitboxParams* out);
