#include "pass_input.h"

#include <errno.h>

#include "api.h"

int pass_input_apply(
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
  if (prev_input_stride_bytes < sizeof(MslInput) || input_stride_bytes < sizeof(MslInput)) {
    return EINVAL;
  }
  return 0;
}
