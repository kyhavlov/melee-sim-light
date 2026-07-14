#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "mpcoll_ecb_points.h"

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
void match_flow_update_post_anim_fighter(MslBatch* batch, int bi, int p);

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
// - Rebirth/EntryStart/EntryEnd run dedicated selector-owned mpColl/custom-ECB entrypoints.
//   refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Coll
//   refs/melee/src/melee/ft/ft_0C31.c::ftCo_EntryStart_Coll
uint8_t match_flow_should_stage_collide(uint16_t action_id);
uint8_t match_flow_entry_custom_ecb_bottom(const MslBatch* batch, size_t idx, float* out_bottom);
uint8_t match_flow_entry_custom_ecb(const MslBatch* batch, size_t idx, MslEcbWorldPoints* out);
uint8_t match_flow_rebirth_stage_collision_runs(const MslBatch* batch, int bi);
void match_flow_rebirth_wait_floor_contact(MslBatch* batch, int bi, int p);

// Match-start fighter input lock (`fp->x221D_b4`) countdown for live init-match episodes only.
// Replay-seeded rows reconstruct the same owner through MslSeed::opening_input_lock_timer instead.
uint8_t match_flow_sim_init_opening_input_lock_timer(void);
