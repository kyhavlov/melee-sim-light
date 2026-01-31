#pragma once

#include "batch_internal.h"

// Throw flow: consume movescript-driven throw flags and apply the throw hit + victim detachment.
//
// Decomp anchor:
// - Throw per-frame callback consumes `throw_flags_b3`/`throw_flags_b4` and triggers release:
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
//
// This module is intentionally small and data-driven:
// - release/flip timing comes from data/moves/{fox,falco}.json via move_tables APIs,
// - damage/KB/state entry is routed through combat_apply_throw_hit().
void throw_flow_update_pre_physics(MslBatch* batch);

