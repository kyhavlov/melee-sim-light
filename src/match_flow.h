#pragma once

#include "batch_internal.h"

// Match flow: KO/death/respawn/entry state machine glue.
//
// This module is intentionally minimal and suite-scoped:
// - Implements suite-present match flow action states (Dead*/Rebirth*/Entry*).
// - Uses ISO/decomp-backed stage bounds and ftCommonData constants.
// - Must remain allocation-free on the hot path.

// Timer-driven match flow transitions + per-frame position adjustments that must happen before the
// per-frame anim/script timebase advance (anim_timebase_update_pre_input).
void match_flow_update_pre_anim(MslBatch* batch);

// Post anim-timebase clamp/overrides (e.g. EntryStart animation end-frame clamping).
void match_flow_update_post_anim(MslBatch* batch);

// Match-flow IASA-style input exits (e.g. RebirthWait -> Fall on any action input).
void match_flow_update_post_input(MslBatch* batch);

// Post-physics match flow: blastzone KO detection and death entry.
void match_flow_update_post_physics(MslBatch* batch);

// Returns 1 if the current action state should run the generic stage-collision pass (floor
// contact, etc).
//
// Decomp shape: match-flow actions use motion-state-specific (or NULL) map/collision callbacks,
// rather than always running the standard grounded/airborne collision paths.
// Examples:
// - Dead* motion states have coll_cb=NULL in ftmotionstates.c (no map collision).
//   refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_DeadDown et al.)
// - Entry coll_cb is an empty function (no map collision).
//   refs/melee/src/melee/ft/ft_0C31.c::ftCo_Entry_Coll
// - Rebirth/EntryStart/EntryEnd run dedicated collision entrypoints (mpColl-based / ECB-based),
//   which we do not yet implement in this lite sim.
//   refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Coll
//   refs/melee/src/melee/ft/ft_0C31.c::ftCo_EntryStart_Coll
uint8_t match_flow_should_stage_collide(uint16_t action_id);
