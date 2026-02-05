#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes);

// Debug-only helper: run step pipeline up to (but excluding) combat_resolve().
// This is used by triage tooling to inspect pose-driven geometry / hitbox event gating before
// a hit mutates action state / anim timebases.
// Contract: this shares the same stage ordering as step_one_frame(), with only combat_resolve()
// skipped, to avoid pipeline drift between debug and runtime paths.
int step_one_frame_pre_combat(MslBatch* batch, const uint8_t* prev_input_bytes,
                              size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                              size_t input_stride_bytes);
