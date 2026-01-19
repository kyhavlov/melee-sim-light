#include "msl_pass_input.h"

#include <errno.h>

#include "msl_api.h"

int msl_pass_input_apply_v0(
    MslBatch* batch,
    const uint8_t* prev_input_bytes,
    size_t prev_input_stride_bytes,
    const uint8_t* input_bytes,
    size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }
  // Empty sim stub: validate pointers/strides only.
  if (prev_input_bytes == NULL || input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInputV0) || input_stride_bytes < sizeof(MslInputV0)) {
    return EINVAL;
  }
  return 0;
}

