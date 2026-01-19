#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

int pass_input_apply(
    MslBatch* batch,
    const uint8_t* prev_input_bytes,
    size_t prev_input_stride_bytes,
    const uint8_t* input_bytes,
    size_t input_stride_bytes);
