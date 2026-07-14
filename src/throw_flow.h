#pragma once

#include "batch_internal.h"
#include "action_ids.h"

// Throw flow: consume movescript-driven throw flags and apply the throw hit + victim detachment.
//
// Decomp anchor:
// - Throw per-frame callback consumes `throw_flags_b3`/`throw_flags_b4` and triggers release:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
//
// This module is intentionally small and data-driven:
// - release/flip timing comes from the live fighter-script command state,
// - damage/KB/state entry is routed through combat_apply_throw_hit().
void throw_flow_update_anim_callback_pre_input(MslBatch* batch, int batch_index, int owner_p);

// Throw ground callback floor-loss continuation (`fn_800DD684`): release the linked victim and
// enter Fall for both fighters.
void throw_flow_ground_loss_release(MslBatch* batch, int batch_index, int owner_p);
void throw_flow_resume_attached_victim_after_hold(MslBatch* batch, int batch_index, int owner_p);
