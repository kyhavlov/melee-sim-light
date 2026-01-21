#pragma once

#include "batch_internal.h"

void combat_resolve(MslBatch* batch);

// Debug/testing helper: run combat pass-1 selection (BODY-only, shield-safe, with rehit
// suppression) and write selected contacts into `out_contacts`.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// with at most 1 record per (attacker, defender) per call.
int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count);
