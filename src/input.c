#include "input.h"

#include <errno.h>

#include "api.h"
#include "ucf.h"

int input_apply(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }
  if (prev_input_bytes == NULL || input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInput) || input_stride_bytes < sizeof(MslInput)) {
    return EINVAL;
  }

  const uint8_t ucf_enabled = batch->config.ucf_enabled ? 1 : 0;
  const uint8_t cardinals = batch->config.ucf_cardinals_1_0_enabled ? 1 : 0;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* prev_ptr = prev_input_bytes + (size_t)bi * prev_input_stride_bytes;
    const uint8_t* cur_ptr = input_bytes + (size_t)bi * input_stride_bytes;
    const MslInput* prev = (const MslInput*)prev_ptr;
    const MslInput* cur = (const MslInput*)cur_ptr;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const uint16_t prev_buttons = prev->p[p].buttons;
      const uint16_t cur_buttons = cur->p[p].buttons;
      batch->state.prev_input_buttons[idx] = prev_buttons;
      batch->state.input_buttons[idx] = cur_buttons;
      batch->state.input_buttons_pressed[idx] = (uint16_t)(cur_buttons & (uint16_t)~prev_buttons);
      batch->state.input_buttons_released[idx] = (uint16_t)(prev_buttons & (uint16_t)~cur_buttons);

      const MslStickI8 main =
          ucf_process_stick_i8(cur->p[p].main_x, cur->p[p].main_y, ucf_enabled, cardinals);
      const MslStickI8 c =
          ucf_process_stick_i8(cur->p[p].c_x, cur->p[p].c_y, ucf_enabled, cardinals);
      batch->state.input_main_x[idx] = main.x;
      batch->state.input_main_y[idx] = main.y;
      batch->state.input_c_x[idx] = c.x;
      batch->state.input_c_y[idx] = c.y;

      batch->state.input_l[idx] = cur->p[p].l;
      batch->state.input_r[idx] = cur->p[p].r;
    }
  }

  return 0;
}
