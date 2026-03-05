#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

int input_apply(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                const uint8_t* input_bytes, size_t input_stride_bytes);

// Pre-input callback snapshot (decomp prio1 ownership before input_cb/prio3):
// - populate input lanes from `prev_input_t` so pre-input animation callbacks can consult prior
//   frame button/stick state deterministically.
// refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
int input_apply_pre_input_snapshot(MslBatch* batch, const uint8_t* prev_input_bytes,
                                   size_t prev_input_stride_bytes);

// UCF x-smash intent heuristic (uses the seeded UCF pad buffer ring).
// refs/ucf/include/ucf/pad_buffer.h::check_ucf_xsmash
uint8_t msl_ucf_check_xsmash(const MslStateSoA* s, size_t idx);
