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
