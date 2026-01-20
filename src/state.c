#include "state.h"

#include <string.h>

#include "alloc.h"

static void state_zero_ptrs(MslStateSoA* s) {
  if (s == NULL) {
    return;
  }
  memset(s, 0, sizeof(*s));
}

int state_alloc(MslStateSoA* state, int batch_size) {
  if (state == NULL || batch_size <= 0) {
    return -1;
  }
  state_zero_ptrs(state);

  const size_t b = (size_t)batch_size;
  const size_t bp = b * (size_t)MSL_MAX_PLAYERS;
  const size_t bi = b * (size_t)MSL_MAX_ITEMS;
  const size_t bp4 = bp * 4u;

  state->frame_id = (int32_t*)alloc_aligned_64(sizeof(int32_t) * b);
  state->frame_pre_random_seed = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->stage_id = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->is_teams = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->team_id = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->char_id = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);

  state->pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->prev_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->prev_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_air_x_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_ground_x_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_y_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_x_attack = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_y_attack = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->facing = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->prev_on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);

  state->action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->action_frame = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->jumps_left = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->stocks = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->kneebend_jump_input = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->kneebend_is_short_hop = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->tilt_timer_x = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->tilt_timer_y = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fall_fast = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->turn_has_turned = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->turn_frames_to_turn = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->lr_press_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x672_input_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x673 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x674 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x675 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x676_x = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x677_y = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x678 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x679_x = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x67A_y = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x67B = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x67C = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x67D = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x67E = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x680 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x681 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x682 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x683 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x684 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);

  state->ucf_padbuf_index = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ucf_padbuf_sdrop_up_frames = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ucf_padbuf_stick_x = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp4);
  state->ucf_padbuf_stick_y = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp4);

  state->percent = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->shield_hp = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->hitlag = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->hitstun = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->l_cancel = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hurtbox_state = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ground_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->animation_index = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bp);
  state->instance_hit_by = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->last_attack_landed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combo_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->last_hit_by = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->state_flags = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp * MSL_STATE_FLAGS_BYTES);

  state->input_buttons = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->prev_input_buttons = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->input_buttons_pressed = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->input_buttons_released = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->input_main_x = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->input_main_y = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->prev_input_main_x = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->prev_input_main_y = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->input_c_x = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->input_c_y = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->input_l = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->input_r = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);

  state->item_exists = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_state = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_type = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_owner = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bi);
  state->item_instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_direction = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_vel_x = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_vel_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_pos_x = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_pos_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_damage = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_timer = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_spawn_id = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bi);
  state->item_misc0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc1 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc2 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc3 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);

  if (!state->frame_id || !state->frame_pre_random_seed || !state->stage_id || !state->is_teams ||
      !state->team_id || !state->char_id || !state->pos_x || !state->pos_y || !state->prev_pos_x ||
      !state->prev_pos_y || !state->speed_air_x_self || !state->speed_ground_x_self ||
      !state->speed_y_self || !state->speed_x_attack || !state->speed_y_attack || !state->facing ||
      !state->on_ground || !state->prev_on_ground || !state->action_id || !state->action_frame ||
      !state->jumps_left || !state->stocks || !state->kneebend_jump_input ||
      !state->kneebend_is_short_hop || !state->tilt_timer_x || !state->tilt_timer_y ||
      !state->fall_fast || !state->turn_has_turned || !state->turn_frames_to_turn ||
      !state->lr_press_timer || !state->x672_input_timer || !state->x673 || !state->x674 ||
      !state->x675 || !state->x676_x || !state->x677_y || !state->x678 || !state->x679_x ||
      !state->x67A_y || !state->x67B || !state->x67C || !state->x67D || !state->x67E ||
      !state->x680 || !state->x681 || !state->x682 || !state->x683 || !state->x684 ||
      !state->ucf_padbuf_index || !state->ucf_padbuf_sdrop_up_frames || !state->ucf_padbuf_stick_x ||
      !state->ucf_padbuf_stick_y ||
      !state->percent || !state->shield_hp || !state->hitlag || !state->hitstun || !state->l_cancel ||
      !state->hurtbox_state || !state->ground_id || !state->animation_index ||
      !state->instance_hit_by || !state->instance_id || !state->last_attack_landed ||
      !state->combo_count || !state->last_hit_by || !state->state_flags || !state->input_buttons ||
      !state->prev_input_buttons || !state->input_buttons_pressed ||
      !state->input_buttons_released || !state->input_main_x || !state->input_main_y ||
      !state->prev_input_main_x || !state->prev_input_main_y || !state->input_c_x ||
      !state->input_c_y || !state->input_l || !state->input_r || !state->item_exists ||
      !state->item_state || !state->item_type || !state->item_owner || !state->item_instance_id ||
      !state->item_direction || !state->item_vel_x || !state->item_vel_y || !state->item_pos_x ||
      !state->item_pos_y || !state->item_damage || !state->item_timer || !state->item_spawn_id ||
      !state->item_misc0 || !state->item_misc1 || !state->item_misc2 || !state->item_misc3) {
    return -1;
  }

  return 0;
}

void state_free(MslStateSoA* state) {
  if (state == NULL) {
    return;
  }
  alloc_free(state->frame_id);
  alloc_free(state->frame_pre_random_seed);
  alloc_free(state->stage_id);
  alloc_free(state->is_teams);
  alloc_free(state->team_id);
  alloc_free(state->char_id);

  alloc_free(state->pos_x);
  alloc_free(state->pos_y);
  alloc_free(state->prev_pos_x);
  alloc_free(state->prev_pos_y);
  alloc_free(state->speed_air_x_self);
  alloc_free(state->speed_ground_x_self);
  alloc_free(state->speed_y_self);
  alloc_free(state->speed_x_attack);
  alloc_free(state->speed_y_attack);
  alloc_free(state->facing);
  alloc_free(state->on_ground);
  alloc_free(state->prev_on_ground);

  alloc_free(state->action_id);
  alloc_free(state->action_frame);
  alloc_free(state->jumps_left);
  alloc_free(state->stocks);
  alloc_free(state->kneebend_jump_input);
  alloc_free(state->kneebend_is_short_hop);
  alloc_free(state->tilt_timer_x);
  alloc_free(state->tilt_timer_y);
  alloc_free(state->fall_fast);
  alloc_free(state->turn_has_turned);
  alloc_free(state->turn_frames_to_turn);
  alloc_free(state->lr_press_timer);
  alloc_free(state->x672_input_timer);
  alloc_free(state->x673);
  alloc_free(state->x674);
  alloc_free(state->x675);
  alloc_free(state->x676_x);
  alloc_free(state->x677_y);
  alloc_free(state->x678);
  alloc_free(state->x679_x);
  alloc_free(state->x67A_y);
  alloc_free(state->x67B);
  alloc_free(state->x67C);
  alloc_free(state->x67D);
  alloc_free(state->x67E);
  alloc_free(state->x680);
  alloc_free(state->x681);
  alloc_free(state->x682);
  alloc_free(state->x683);
  alloc_free(state->x684);

  alloc_free(state->ucf_padbuf_index);
  alloc_free(state->ucf_padbuf_sdrop_up_frames);
  alloc_free(state->ucf_padbuf_stick_x);
  alloc_free(state->ucf_padbuf_stick_y);

  alloc_free(state->percent);
  alloc_free(state->shield_hp);
  alloc_free(state->hitlag);
  alloc_free(state->hitstun);
  alloc_free(state->l_cancel);
  alloc_free(state->hurtbox_state);
  alloc_free(state->ground_id);
  alloc_free(state->animation_index);
  alloc_free(state->instance_hit_by);
  alloc_free(state->instance_id);
  alloc_free(state->last_attack_landed);
  alloc_free(state->combo_count);
  alloc_free(state->last_hit_by);
  alloc_free(state->state_flags);

  alloc_free(state->input_buttons);
  alloc_free(state->prev_input_buttons);
  alloc_free(state->input_buttons_pressed);
  alloc_free(state->input_buttons_released);
  alloc_free(state->input_main_x);
  alloc_free(state->input_main_y);
  alloc_free(state->prev_input_main_x);
  alloc_free(state->prev_input_main_y);
  alloc_free(state->input_c_x);
  alloc_free(state->input_c_y);
  alloc_free(state->input_l);
  alloc_free(state->input_r);

  alloc_free(state->item_exists);
  alloc_free(state->item_state);
  alloc_free(state->item_type);
  alloc_free(state->item_owner);
  alloc_free(state->item_instance_id);
  alloc_free(state->item_direction);
  alloc_free(state->item_vel_x);
  alloc_free(state->item_vel_y);
  alloc_free(state->item_pos_x);
  alloc_free(state->item_pos_y);
  alloc_free(state->item_damage);
  alloc_free(state->item_timer);
  alloc_free(state->item_spawn_id);
  alloc_free(state->item_misc0);
  alloc_free(state->item_misc1);
  alloc_free(state->item_misc2);
  alloc_free(state->item_misc3);

  state_zero_ptrs(state);
}
