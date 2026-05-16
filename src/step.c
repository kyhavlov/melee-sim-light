#include "step.h"

#include "fighter_callbacks.h"

int step_one_frame(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                   const uint8_t* input_bytes, size_t input_stride_bytes) {
  return fighter_callbacks_step_frame(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                      input_stride_bytes, 1u);
}

int step_one_frame_pre_combat(MslBatch* batch, const uint8_t* prev_input_bytes,
                              size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                              size_t input_stride_bytes) {
  return fighter_callbacks_step_frame(batch, prev_input_bytes, prev_input_stride_bytes, input_bytes,
                                      input_stride_bytes, 0u);
}
