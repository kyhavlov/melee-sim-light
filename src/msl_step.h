#pragma once

#include <stddef.h>
#include <stdint.h>

#include "msl_batch_internal.h"

int msl_step_one_frame_v0(
    MslBatch* batch,
    const uint8_t* prev_input_bytes,
    size_t prev_input_stride_bytes,
    const uint8_t* input_bytes,
    size_t input_stride_bytes);

