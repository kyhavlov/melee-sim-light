#include "state.h"

#include <string.h>

#include "alloc.h"
#include "hitlist.h"

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
  const size_t b2 = b * 2u;
  const size_t bp = b * (size_t)MSL_MAX_PLAYERS;
  const size_t bi = b * (size_t)MSL_MAX_ITEMS;
  const size_t bp4 = bp * 4u;
  const size_t bpp = bp * (size_t)MSL_MAX_PLAYERS;
  const size_t bpc = bp * (size_t)MSL_MAX_HURTCAPS;
  const size_t bph = bp * (size_t)MSL_MAX_HITBOXES;
  const size_t bphl = bpp * (size_t)MSL_HITLIST_GROUPS;
  const size_t bphv = bph * (size_t)MSL_MAX_PLAYERS;
  const size_t bpst = bp * (size_t)MSL_STALE_QUEUE_SIZE;
  const size_t bpdn = bp * (size_t)MSL_MAX_DYNAMIC_NODES;

  state->frame_id = (int32_t*)alloc_aligned_64(sizeof(int32_t) * b);
  state->frame_pre_random_seed = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->stage_id = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->stage_fod_platform_height = (float*)alloc_aligned_64(sizeof(float) * b2);
  state->stage_fod_platform_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b2);
  state->stage_fod_platform_velocity = (float*)alloc_aligned_64(sizeof(float) * b2);
  state->stage_fod_platform_velocity_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b2);
  state->stage_fod_platform_scheduler_phase = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b2);
  state->stage_fod_platform_scheduler_timer = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * b2);
  state->stage_fod_platform_scheduler_target = (float*)alloc_aligned_64(sizeof(float) * b2);
  state->stage_fod_platform_scheduler_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b2);
  state->stage_yoshi_shyguy_timer = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * b);
  state->stage_yoshi_shyguy_pattern = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->stage_yoshi_shyguy_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->stage_dream_whispy_wind_dir = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->stage_dream_whispy_wind_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->opening_input_lock_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->stale_attack_instance_counter = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * b);
  state->instance_id_counter = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * b);
  state->item_spawn_id_counter = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->match_damage_ratio = (float*)alloc_aligned_64(sizeof(float) * b);
  state->is_teams = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * b);
  state->team_id = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->char_id = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->handicap = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attack_ratio = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->defense_ratio = (float*)alloc_aligned_64(sizeof(float) * bp);

  state->pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->pos_z = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos0_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos0_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos1_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos1_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos2_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->illusion_ghost_pos2_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->prev_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->prev_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->floor_sweep_prev_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->floor_sweep_prev_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->floor_sweep_seed_prev_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->floor_sweep_seed_prev_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->floor_sweep_seed_prev_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->coll_ecb_bottom_rel_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_prev_ecb_bottom_rel_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_desired_ecb_bottom_rel_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_ecb_bottom_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->coll_prev_ecb_bottom_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->coll_desired_ecb_bottom_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->coll_stage_prev_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_stage_prev_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_stage_cur_pos_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->coll_stage_cur_pos_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_air_x_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_ground_x_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_y_self = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_x_attack = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->speed_y_attack = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->rebound_ground_accel_2 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->rebound_anim_rate_fp_q16_16 = (int32_t*)alloc_aligned_64(sizeof(int32_t) * bp);
  state->specialhi_rotate_model = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->specialhi_rotate_model_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fighter_scale_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->facing = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->facing_dir1 = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->ground_friction_mul = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->kb_smashcharge_active = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->smash_charge_state = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->smash_charge_frames = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->smash_charge_hold_frames_max = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->smash_charge_saved_rate_fp_q16_16 = (int32_t*)alloc_aligned_64(sizeof(int32_t) * bp);
  state->on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->frame_start_on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->prev_on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ground_contact_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ground_contact_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ground_normal_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ground_normal_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->wall_contact_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->wall_contact_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->wall_normal_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->wall_normal_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->wall_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->wall_kind = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->damage_hitlag_wall_asdi_latch = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ceiling_contact_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ceiling_contact_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ceiling_normal_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ceiling_normal_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ceiling_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->coll_env_flags = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bp);
  state->coll_prev_env_flags = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bp);

  state->action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->seed_prev_action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->seed_prev_action_frame = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->prev_action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->prev_action_frame = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->action_frame = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->throw_pulse_consumed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_pulse_crossed_prev_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_command_pending_pulse_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_command_pending_seed_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_pulse_crossed_curr_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->source_clear_timer_x18c8 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->source_clear_owner_set_phase = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->source_clear_processhit_damage_pending_phase =
      (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fighter_8006cda4_pre_gate_consume_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->source_clear_grounded_damage_clear_phase =
      (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->source_clear_terminal_phase = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_pending_victim_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_pending_hit_idx = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attached_victim_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->grab_owner_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->grab_mash_stick_x_sign = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->grab_mash_stick_y_sign = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->grab_offset_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->grab_offset_z = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->thrown_attached_prev_on_ground = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->thrown_attached_prev_ground_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->match_flow_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->entry_end_fall_lock = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->camera_box_visible_x221f_b0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->rebirth_camera_anchor_y_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->camera_target_world_x_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->camera_target_world_y_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->camera_target_world_z_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->camera_box_radius_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->camera_target_point_inside_stage_cam_bounds_u8 =
      (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->magnify_damage_counter_x1910 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->downwait_timer = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->passivewall_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->walljump_input_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->walljump_wall_side_i8 = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->walljump_seed_phase_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->anim_frame_f32 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->anim_frame_fp_q16_16 = (int32_t*)alloc_aligned_64(sizeof(int32_t) * bp);
  state->frame_speed_mul_fp_q16_16 = (int32_t*)alloc_aligned_64(sizeof(int32_t) * bp);
  state->walk_anim_source_vel = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->walk_retarget_tick_source_vel = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->run_anim_source_vel = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->turn_kneebend_facing_override = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->capture_grab_timer = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->capture_wait_counter = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->capture_wait_anim_rate_timer = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->capture_wait_jump_latch = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->capture_breakout_pending = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->throw_anim_rate_fp_q16_16 = (int32_t*)alloc_aligned_64(sizeof(int32_t) * bp);
  state->anim_defer_tick_once = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->jumps_left = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->stocks = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_tilt_x8 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->guard_tilt_x4 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->guard_on_entered_this_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_entry_via_wait_callback = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_entry_via_dash_91ad8 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_x10_frame_start = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_entry_dash_terminal_scalar =
      (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_seed_shield_desc_active = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->state_flags_2218_frame_start = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_jump_oos_entered_this_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->shine_jump_iasa_entered_this_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_timer_x14 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_timer_x18 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_timer_x14_seed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_timer_x18_seed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_reflect_origin_guardon = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_special_enable_timer_x1c = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_release_latched_xc = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_x10 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->lightshield_amount = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->guard_setoff_hitlag_damage_min = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combat_shield_hit_int_damage = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combat_shield_damage_taken = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_setoff_hitlag_exit_phase_u8 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->guard_setoff_post_hitlag_owner_u8 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->kneebend_jump_input = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->kneebend_is_short_hop = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->tilt_timer_x = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->tilt_timer_y = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fall_fast = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attackdash_x0 = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->jab_x0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->jab_rapid_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attack100_x0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attack100_x4 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->run_x0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->runbrake_cmd0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dash_x4 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->shine_release_lag = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->shine_is_release = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ecb_lock_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->ledge_side = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->stage_ledge_occupant_left = (int8_t*)alloc_aligned_64(sizeof(int8_t) * b);
  state->stage_ledge_occupant_right = (int8_t*)alloc_aligned_64(sizeof(int8_t) * b);
  state->ledge_cooldown = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fallspecial_xc = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->fallspecial_landing_lag = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->landing_fallspecial_allow_interrupt = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->turn_has_turned = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->turn_frames_to_turn = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->walk_use_raw_input_once = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->turn_x8 = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->lr_press_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x672_input_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x673 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x674 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x675 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x676_x = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->x2228_b7 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
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
  state->percent_temp = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->phantom_damage_pending_x1898 = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->phantom_damage_timer_x189c = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->phantom_damage_source_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->damage_time_since_hit_x18ac = (int16_t*)alloc_aligned_64(sizeof(int16_t) * bp);
  state->dmg_x2225_b7 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dmg_x2224_b2 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->shield_hp = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->hitlag = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->hitlag_pre_timer = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hitlag_started_frame = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->damage_hitlag_floorhug_latch = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->damage_hitlag_downward_sdi_consumed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hitstun = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->damage_jump_buffer_x14 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->damage_post_hitlag_cb_kind = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->attacker_shield_ground_kb_vel = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->l_cancel = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hurtbox_state = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->colanim_hit_status_x198c = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->colanim_timer_x1990 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->colanim_timer_x1994 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->colanim_lock_x2221_b0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->colanim_hitstun_x198c1_seed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->colanim_terminal_x1990_item_body_guard = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hurtcap_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hurtcap_geometry_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hurtcap_a_x = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_a_y = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_a_z = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_b_x = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_b_y = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_b_z = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_radius = (float*)alloc_aligned_64(sizeof(float) * bpc);
  state->hurtcap_enabled = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bpc);
  state->hurtcap_is_grabbable = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bpc);
  state->hurtcap_height = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bpc);
  state->hitbox_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hitbox_enabled = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_prev_enabled = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_prev_x = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_prev_y = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_prev_z = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_pose_create = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_enable_edge = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_x43_b2 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_prev_bootstrap = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->hitbox_x = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_y = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_z = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_radius = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_damage = (float*)alloc_aligned_64(sizeof(float) * bph);
  state->hitbox_bone_part_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_0 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_1 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_2 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_3 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_4 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_5 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_6 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_u16_7 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_angle = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_kbg = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_wsk = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_bkb = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->hitbox_element = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_shield_damage = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bph);
  state->hitbox_sfx_severity = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_sfx_kind = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->hitbox_flags = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bph);
  state->shield_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->shield_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->shield_z = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->shield_radius = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->reflector_x = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->reflector_y = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->reflector_radius = (float*)alloc_aligned_64(sizeof(float) * bp);
  state->ground_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->floor_skip_segment_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->animation_index = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bp);
  state->dynamic_pose_state_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dynamic_pose_apply_collision_matrix = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dynamic_pose_node_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dynamic_pose_char_id = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->dynamic_pose_msid = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->dynamic_pose_frame = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->dynamic_pose_rot_x = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_rot_y = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_rot_z = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_pos_x = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_pos_y = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_pos_z = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_axis_x = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_axis_y = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_axis_z = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->dynamic_pose_angle = (float*)alloc_aligned_64(sizeof(float) * bpdn);
  state->instance_hit_by = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->instance_id_x2073 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->motion_entry_instance_id_override = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->instance_identity_last_action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->attack_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->attack_instance = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->attack_identity_last_action_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->last_attack_landed = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combo_count = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combo_victim_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->combo_victim_instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->combo_timer_x2098 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->combo_push_timer_x2092 = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bp);
  state->source_port0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->last_hit_by = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->state_flags = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp * MSL_STATE_FLAGS_BYTES);
  state->combat_hitlist_cd = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bphl);
  state->combat_hitlist_victim_iid = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bphl);
  state->combat_hitlist_hb_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bph);
  state->combat_hitlist_hb_cd = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bphv);
  state->combat_hitlist_hb_victim_iid = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bphv);
  state->combat_shield_contact_hb_kind = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bphv);
  state->hitlist_reseed_gen = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * b);
  state->fighter_hitlist = (MslHitlistCapsule*)alloc_aligned_64(sizeof(MslHitlistCapsule) * bph);
  state->fighter_hitlist_init_gen = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bph);
  state->stale_queue_index = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->stale_move_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bpst);
  state->stale_attack_instance = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bpst);

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
  state->prev_input_c_x = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->prev_input_c_y = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bp);
  state->prev_input_l = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->prev_input_r = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->input_l = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);
  state->input_r = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bp);

  state->item_exists = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_state = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_type = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_owner = (int8_t*)alloc_aligned_64(sizeof(int8_t) * bi);
  state->item_instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_attack_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_attack_instance = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_direction = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_vel_x = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_vel_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_pos_x = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_pos_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_damage = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_reflect_damage_mul = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_timer = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_hitlag = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_spawn_id = (uint32_t*)alloc_aligned_64(sizeof(uint32_t) * bi);
  state->item_misc0 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc1 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc2 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_misc3 = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_pending_reflect_owner_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_pending_reflect_instance_id = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_reflect_transfer_seed_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_reflect_transfer_seed_iid = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_shield_bounce_seed_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shield_bounce_seed_vel_x = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_shield_bounce_seed_vel_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_hidden_body_hit_victim_port = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_hidden_body_hit_hurt_height = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_hidden_callback_flags = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_prev_vel_y = (float*)alloc_aligned_64(sizeof(float) * bi);
  state->item_shyguy_prev_vel_y_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_dyn_y_phase = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_dyn_y_phase_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_speed_index = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_speed_index_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_delay = (uint16_t*)alloc_aligned_64(sizeof(uint16_t) * bi);
  state->item_shyguy_delay_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_hitlag = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_shyguy_hitlag_valid = (uint8_t*)alloc_aligned_64(sizeof(uint8_t) * bi);
  state->item_hitlist = (MslHitlistCapsule*)alloc_aligned_64(sizeof(MslHitlistCapsule) * bi *
                                                             (size_t)MSL_MAX_HITBOXES);

  if (!state->frame_id || !state->frame_pre_random_seed || !state->stage_id ||
      !state->stage_fod_platform_height || !state->stage_fod_platform_valid ||
      !state->stage_fod_platform_velocity || !state->stage_fod_platform_velocity_valid ||
      !state->stage_fod_platform_scheduler_phase || !state->stage_fod_platform_scheduler_timer ||
      !state->stage_fod_platform_scheduler_target || !state->stage_fod_platform_scheduler_valid ||
      !state->stage_yoshi_shyguy_timer || !state->stage_yoshi_shyguy_pattern ||
      !state->stage_yoshi_shyguy_valid || !state->stage_dream_whispy_wind_dir ||
      !state->stage_dream_whispy_wind_valid || !state->opening_input_lock_timer ||
      !state->stale_attack_instance_counter || !state->instance_id_counter ||
      !state->item_spawn_id_counter || !state->match_damage_ratio || !state->is_teams ||
      !state->team_id || !state->char_id || !state->handicap || !state->attack_ratio ||
      !state->defense_ratio || !state->pos_x || !state->pos_y || !state->pos_z ||
      !state->illusion_ghost_pos0_x || !state->illusion_ghost_pos0_y ||
      !state->illusion_ghost_pos1_x || !state->illusion_ghost_pos1_y ||
      !state->illusion_ghost_pos2_x || !state->illusion_ghost_pos2_y || !state->prev_pos_x ||
      !state->prev_pos_y || !state->floor_sweep_prev_pos_x || !state->floor_sweep_prev_pos_y ||
      !state->floor_sweep_seed_prev_pos_x || !state->floor_sweep_seed_prev_pos_y ||
      !state->floor_sweep_seed_prev_valid || !state->coll_ecb_bottom_rel_y ||
      !state->coll_prev_ecb_bottom_rel_y || !state->coll_desired_ecb_bottom_rel_y ||
      !state->coll_ecb_bottom_valid || !state->coll_prev_ecb_bottom_valid ||
      !state->coll_desired_ecb_bottom_valid || !state->coll_stage_prev_pos_x ||
      !state->coll_stage_prev_pos_y || !state->coll_stage_cur_pos_x ||
      !state->coll_stage_cur_pos_y || !state->speed_air_x_self || !state->speed_ground_x_self ||
      !state->speed_y_self || !state->speed_x_attack || !state->speed_y_attack ||
      !state->specialhi_rotate_model || !state->specialhi_rotate_model_valid ||
      !state->fighter_scale_y || !state->facing || !state->facing_dir1 ||
      !state->ground_friction_mul || !state->kb_smashcharge_active || !state->smash_charge_state ||
      !state->smash_charge_frames || !state->smash_charge_hold_frames_max ||
      !state->smash_charge_saved_rate_fp_q16_16 || !state->on_ground ||
      !state->frame_start_on_ground || !state->prev_on_ground || !state->ground_contact_x ||
      !state->ground_contact_y || !state->ground_normal_x || !state->ground_normal_y ||
      !state->wall_contact_x || !state->wall_contact_y || !state->wall_normal_x ||
      !state->wall_normal_y || !state->wall_id || !state->wall_kind ||
      !state->damage_hitlag_wall_asdi_latch || !state->ceiling_contact_x ||
      !state->ceiling_contact_y || !state->ceiling_normal_x || !state->ceiling_normal_y ||
      !state->ceiling_id || !state->coll_env_flags || !state->coll_prev_env_flags ||
      !state->action_id || !state->seed_prev_action_id || !state->seed_prev_action_frame ||
      !state->prev_action_id || !state->prev_action_frame || !state->action_frame ||
      !state->throw_pending_victim_port || !state->throw_pending_hit_idx ||
      !state->attached_victim_port || !state->throw_pulse_consumed ||
      !state->throw_pulse_crossed_prev_frame || !state->throw_command_pending_pulse_frame ||
      !state->throw_command_pending_seed_valid || !state->throw_pulse_crossed_curr_frame ||
      !state->source_clear_timer_x18c8 || !state->source_clear_owner_set_phase ||
      !state->source_clear_processhit_damage_pending_phase ||
      !state->fighter_8006cda4_pre_gate_consume_count ||
      !state->source_clear_grounded_damage_clear_phase || !state->source_clear_terminal_phase ||
      !state->grab_mash_stick_x_sign || !state->grab_mash_stick_y_sign ||
      !state->match_flow_timer || !state->entry_end_fall_lock ||
      !state->camera_box_visible_x221f_b0 || !state->rebirth_camera_anchor_y_f32 ||
      !state->camera_target_world_x_f32 || !state->camera_target_world_y_f32 ||
      !state->camera_target_world_z_f32 || !state->camera_box_radius_f32 ||
      !state->camera_target_point_inside_stage_cam_bounds_u8 ||
      !state->magnify_damage_counter_x1910 || !state->downwait_timer || !state->passivewall_timer ||
      !state->walljump_input_timer || !state->walljump_wall_side_i8 ||
      !state->walljump_seed_phase_valid || !state->anim_frame_f32 || !state->anim_frame_fp_q16_16 ||
      !state->frame_speed_mul_fp_q16_16 || !state->walk_anim_source_vel ||
      !state->walk_retarget_tick_source_vel || !state->run_anim_source_vel ||
      !state->rebound_ground_accel_2 || !state->rebound_anim_rate_fp_q16_16 ||
      !state->turn_kneebend_facing_override || !state->capture_grab_timer ||
      !state->capture_wait_counter || !state->capture_wait_anim_rate_timer ||
      !state->capture_wait_jump_latch || !state->capture_breakout_pending ||
      !state->throw_anim_rate_fp_q16_16 || !state->anim_defer_tick_once || !state->jumps_left ||
      !state->stocks || !state->guard_tilt_x8 || !state->guard_tilt_x4 ||
      !state->guard_on_entered_this_frame || !state->guard_entry_via_wait_callback ||
      !state->guard_entry_via_dash_91ad8 || !state->guard_x10_frame_start ||
      !state->guard_seed_shield_desc_active || !state->state_flags_2218_frame_start ||
      !state->guard_jump_oos_entered_this_frame || !state->shine_jump_iasa_entered_this_frame ||
      !state->guard_reflect_timer_x14 || !state->guard_reflect_timer_x18 ||
      !state->guard_reflect_timer_x14_seed || !state->guard_reflect_timer_x18_seed ||
      !state->guard_reflect_origin_guardon || !state->guard_special_enable_timer_x1c ||
      !state->guard_release_latched_xc || !state->guard_x10 || !state->lightshield_amount ||
      !state->guard_setoff_hitlag_damage_min || !state->guard_setoff_hitlag_exit_phase_u8 ||
      !state->guard_setoff_post_hitlag_owner_u8 || !state->kneebend_jump_input ||
      !state->guard_reflect_entry_dash_terminal_scalar || !state->kneebend_is_short_hop ||
      !state->tilt_timer_x || !state->tilt_timer_y || !state->fall_fast || !state->attackdash_x0 ||
      !state->jab_x0 || !state->jab_rapid_count || !state->attack100_x0 || !state->attack100_x4 ||
      !state->run_x0 || !state->runbrake_cmd0 || !state->dash_x4 || !state->shine_release_lag ||
      !state->shine_is_release || !state->ecb_lock_timer || !state->ledge_side ||
      !state->stage_ledge_occupant_left || !state->stage_ledge_occupant_right ||
      !state->ledge_cooldown || !state->fallspecial_xc || !state->fallspecial_landing_lag ||
      !state->landing_fallspecial_allow_interrupt || !state->turn_has_turned ||
      !state->turn_frames_to_turn || !state->walk_use_raw_input_once || !state->turn_x8 ||
      !state->lr_press_timer || !state->x672_input_timer || !state->x673 || !state->x674 ||
      !state->x675 || !state->x676_x || !state->x2228_b7 || !state->x677_y || !state->x678 ||
      !state->x679_x || !state->x67A_y || !state->x67B || !state->x67C || !state->x67D ||
      !state->x67E || !state->x680 || !state->x681 || !state->x682 || !state->x683 ||
      !state->x684 || !state->ucf_padbuf_index || !state->ucf_padbuf_sdrop_up_frames ||
      !state->ucf_padbuf_stick_x || !state->ucf_padbuf_stick_y || !state->percent ||
      !state->percent_temp || !state->phantom_damage_pending_x1898 ||
      !state->phantom_damage_timer_x189c || !state->phantom_damage_source_port ||
      !state->damage_time_since_hit_x18ac || !state->dmg_x2225_b7 || !state->dmg_x2224_b2 ||
      !state->shield_hp || !state->hitlag || !state->hitlag_pre_timer ||
      !state->hitlag_started_frame || !state->damage_hitlag_floorhug_latch ||
      !state->damage_hitlag_downward_sdi_consumed || !state->hitstun ||
      !state->damage_jump_buffer_x14 || !state->damage_post_hitlag_cb_kind ||
      !state->attacker_shield_ground_kb_vel || !state->l_cancel || !state->hurtbox_state ||
      !state->colanim_hit_status_x198c || !state->colanim_timer_x1990 ||
      !state->colanim_timer_x1994 || !state->colanim_lock_x2221_b0 ||
      !state->colanim_hitstun_x198c1_seed || !state->colanim_terminal_x1990_item_body_guard ||
      !state->hurtcap_count || !state->hurtcap_geometry_valid || !state->hurtcap_a_x ||
      !state->hurtcap_a_y || !state->hurtcap_a_z || !state->hurtcap_b_x || !state->hurtcap_b_y ||
      !state->hurtcap_b_z || !state->hurtcap_radius || !state->hurtcap_enabled ||
      !state->hurtcap_is_grabbable || !state->hurtcap_height || !state->hitbox_count ||
      !state->hitbox_enabled || !state->hitbox_prev_enabled || !state->hitbox_prev_x ||
      !state->hitbox_prev_y || !state->hitbox_prev_z || !state->hitbox_pose_create ||
      !state->hitbox_enable_edge || !state->hitbox_x43_b2 || !state->hitbox_prev_bootstrap ||
      !state->hitbox_x || !state->hitbox_y || !state->hitbox_z || !state->hitbox_radius ||
      !state->hitbox_damage || !state->hitbox_bone_part_id || !state->hitbox_u16_0 ||
      !state->hitbox_u16_1 || !state->hitbox_u16_2 || !state->hitbox_u16_3 ||
      !state->hitbox_u16_4 || !state->hitbox_u16_5 || !state->hitbox_u16_6 ||
      !state->hitbox_u16_7 || !state->hitbox_angle || !state->hitbox_kbg || !state->hitbox_wsk ||
      !state->hitbox_bkb || !state->hitbox_element || !state->hitbox_shield_damage ||
      !state->hitbox_sfx_severity || !state->hitbox_sfx_kind || !state->hitbox_flags ||
      !state->shield_x || !state->shield_y || !state->shield_z || !state->shield_radius ||
      !state->reflector_x || !state->reflector_y || !state->reflector_radius || !state->ground_id ||
      !state->floor_skip_segment_id || !state->animation_index ||
      !state->dynamic_pose_state_valid || !state->dynamic_pose_apply_collision_matrix ||
      !state->dynamic_pose_node_count || !state->dynamic_pose_char_id ||
      !state->dynamic_pose_msid || !state->dynamic_pose_frame || !state->dynamic_pose_rot_x ||
      !state->dynamic_pose_rot_y || !state->dynamic_pose_rot_z || !state->dynamic_pose_pos_x ||
      !state->dynamic_pose_pos_y || !state->dynamic_pose_pos_z || !state->dynamic_pose_axis_x ||
      !state->dynamic_pose_axis_y || !state->dynamic_pose_axis_z || !state->dynamic_pose_angle ||
      !state->instance_hit_by || !state->instance_id || !state->instance_id_x2073 ||
      !state->motion_entry_instance_id_override || !state->instance_identity_last_action_id ||
      !state->attack_id || !state->attack_instance || !state->attack_identity_last_action_id ||
      !state->last_attack_landed || !state->combo_count || !state->combo_victim_port ||
      !state->combo_victim_instance_id || !state->combo_timer_x2098 ||
      !state->combo_push_timer_x2092 || !state->source_port0 || !state->last_hit_by ||
      !state->state_flags || !state->combat_hitlist_cd || !state->combat_hitlist_victim_iid ||
      !state->combat_hitlist_hb_valid || !state->combat_hitlist_hb_cd ||
      !state->combat_hitlist_hb_victim_iid || !state->combat_shield_contact_hb_kind ||
      !state->combat_shield_hit_int_damage || !state->combat_shield_damage_taken ||
      !state->hitlist_reseed_gen || !state->fighter_hitlist || !state->fighter_hitlist_init_gen ||
      !state->stale_queue_index || !state->stale_move_id || !state->stale_attack_instance ||
      !state->input_buttons || !state->prev_input_buttons || !state->input_buttons_pressed ||
      !state->input_buttons_released || !state->input_main_x || !state->input_main_y ||
      !state->prev_input_main_x || !state->prev_input_main_y || !state->input_c_x ||
      !state->input_c_y || !state->prev_input_c_x || !state->prev_input_c_y ||
      !state->prev_input_l || !state->prev_input_r || !state->input_l || !state->input_r ||
      !state->item_exists || !state->item_state || !state->item_type || !state->item_owner ||
      !state->item_instance_id || !state->item_attack_id || !state->item_attack_instance ||
      !state->item_direction || !state->item_vel_x || !state->item_vel_y || !state->item_pos_x ||
      !state->item_pos_y || !state->item_damage || !state->item_reflect_damage_mul ||
      !state->item_timer || !state->item_hitlag || !state->item_spawn_id || !state->item_misc0 ||
      !state->item_misc1 || !state->item_misc2 || !state->item_misc3 ||
      !state->item_pending_reflect_owner_port || !state->item_pending_reflect_instance_id ||
      !state->item_reflect_transfer_seed_port || !state->item_reflect_transfer_seed_iid ||
      !state->item_shield_bounce_seed_valid || !state->item_shield_bounce_seed_vel_x ||
      !state->item_shield_bounce_seed_vel_y || !state->item_hidden_body_hit_victim_port ||
      !state->item_hidden_body_hit_hurt_height || !state->item_hidden_callback_flags ||
      !state->item_shyguy_prev_vel_y || !state->item_shyguy_prev_vel_y_valid ||
      !state->item_shyguy_dyn_y_phase || !state->item_shyguy_dyn_y_phase_valid ||
      !state->item_shyguy_speed_index || !state->item_shyguy_speed_index_valid ||
      !state->item_shyguy_delay || !state->item_shyguy_delay_valid || !state->item_shyguy_hitlag ||
      !state->item_shyguy_hitlag_valid || !state->item_hitlist) {
    return -1;
  }

  // Initialize hitlists to a known-empty state (entry.kind_slot == 0xFF).
  for (size_t i = 0; i < b; i++) {
    state->hitlist_reseed_gen[i] = 1u;
  }
  memset(state->frame_id, 0, sizeof(int32_t) * b);
  memset(state->stage_fod_platform_scheduler_phase, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_scheduler_timer, 0, sizeof(uint16_t) * b2);
  memset(state->stage_fod_platform_scheduler_target, 0, sizeof(float) * b2);
  memset(state->stage_fod_platform_scheduler_valid, 0, sizeof(uint8_t) * b2);
  memset(state->stage_dream_whispy_wind_dir, 0, sizeof(uint8_t) * b);
  memset(state->stage_dream_whispy_wind_valid, 0, sizeof(uint8_t) * b);
  memset(state->item_spawn_id_counter, 0, sizeof(uint32_t) * b);
  memset(state->dynamic_pose_state_valid, 0, sizeof(uint8_t) * bp);
  memset(state->dynamic_pose_apply_collision_matrix, 0, sizeof(uint8_t) * bp);
  memset(state->dynamic_pose_node_count, 0, sizeof(uint8_t) * bp);
  memset(state->dynamic_pose_char_id, 0, sizeof(uint8_t) * bp);
  memset(state->dynamic_pose_msid, 0, sizeof(uint16_t) * bp);
  memset(state->dynamic_pose_frame, 0, sizeof(uint16_t) * bp);
  memset(state->dynamic_pose_rot_x, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_rot_y, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_rot_z, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_pos_x, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_pos_y, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_pos_z, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_axis_x, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_axis_y, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_axis_z, 0, sizeof(float) * bpdn);
  memset(state->dynamic_pose_angle, 0, sizeof(float) * bpdn);
  memset(state->capture_grab_timer, 0, sizeof(float) * bp);
  memset(state->capture_wait_counter, 0, sizeof(float) * bp);
  memset(state->capture_wait_anim_rate_timer, 0, sizeof(float) * bp);
  memset(state->capture_wait_jump_latch, 0, sizeof(uint8_t) * bp);
  memset(state->capture_breakout_pending, 0, sizeof(uint8_t) * bp);
  memset(state->guard_reflect_entry_dash_terminal_scalar, 0, sizeof(uint8_t) * bp);
  memset(state->guard_seed_shield_desc_active, 0, sizeof(uint8_t) * bp);
  memset(state->guard_special_enable_timer_x1c, 0, sizeof(uint8_t) * bp);
  memset(state->throw_anim_rate_fp_q16_16, 0, sizeof(int32_t) * bp);
  memset(state->throw_command_pending_pulse_frame, 0, sizeof(uint8_t) * bp);
  memset(state->throw_command_pending_seed_valid, 0, sizeof(uint8_t) * bp);
  memset(state->throw_pulse_crossed_curr_frame, 0, sizeof(uint8_t) * bp);
  memset(state->magnify_damage_counter_x1910, 0, sizeof(uint16_t) * bp);
  memset(state->smash_charge_state, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_frames, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_hold_frames_max, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_saved_rate_fp_q16_16, 0, sizeof(int32_t) * bp);
  memset(state->walljump_seed_phase_valid, 0, sizeof(uint8_t) * bp);
  memset(state->specialhi_rotate_model, 0, sizeof(float) * bp);
  memset(state->specialhi_rotate_model_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_prev_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_desired_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->walk_anim_source_vel, 0, sizeof(float) * bp);
  memset(state->walk_retarget_tick_source_vel, 0, sizeof(float) * bp);
  memset(state->run_anim_source_vel, 0, sizeof(float) * bp);
  memset(state->turn_kneebend_facing_override, 0, sizeof(uint8_t) * bp);
  memset(state->walk_use_raw_input_once, 0, sizeof(uint8_t) * bp);
  memset(state->x2228_b7, 0, sizeof(uint8_t) * bp);
  memset(state->fallspecial_landing_lag, 0, sizeof(float) * bp);
  memset(state->damage_hitlag_wall_asdi_latch, 0, sizeof(uint8_t) * bp);
  memset(state->damage_hitlag_floorhug_latch, 0, sizeof(uint8_t) * bp);
  memset(state->damage_hitlag_downward_sdi_consumed, 0, sizeof(uint8_t) * bp);
  memset(state->phantom_damage_pending_x1898, 0, sizeof(float) * bp);
  memset(state->phantom_damage_timer_x189c, 0, sizeof(uint16_t) * bp);
  memset(state->phantom_damage_source_port, 0xFF, sizeof(uint8_t) * bp);
  for (size_t i = 0; i < bp; i++) {
    state->floor_skip_segment_id[i] = 0xFFFFu;
  }
  for (size_t i = 0; i < bph; i++) {
    state->fighter_hitlist_init_gen[i] = 0u;
    hitlist_capsule_clear(&state->fighter_hitlist[i]);
  }
  for (size_t i = 0; i < bi; i++) {
    state->item_hitlag[i] = 0u;
  }
  for (size_t i = 0; i < bi * (size_t)MSL_MAX_HITBOXES; i++) {
    hitlist_capsule_clear(&state->item_hitlist[i]);
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
  alloc_free(state->stage_fod_platform_height);
  alloc_free(state->stage_fod_platform_valid);
  alloc_free(state->stage_fod_platform_velocity);
  alloc_free(state->stage_fod_platform_velocity_valid);
  alloc_free(state->stage_fod_platform_scheduler_phase);
  alloc_free(state->stage_fod_platform_scheduler_timer);
  alloc_free(state->stage_fod_platform_scheduler_target);
  alloc_free(state->stage_fod_platform_scheduler_valid);
  alloc_free(state->stage_yoshi_shyguy_timer);
  alloc_free(state->stage_yoshi_shyguy_pattern);
  alloc_free(state->stage_yoshi_shyguy_valid);
  alloc_free(state->stage_dream_whispy_wind_dir);
  alloc_free(state->stage_dream_whispy_wind_valid);
  alloc_free(state->opening_input_lock_timer);
  alloc_free(state->stale_attack_instance_counter);
  alloc_free(state->instance_id_counter);
  alloc_free(state->item_spawn_id_counter);
  alloc_free(state->match_damage_ratio);
  alloc_free(state->is_teams);
  alloc_free(state->team_id);
  alloc_free(state->char_id);
  alloc_free(state->handicap);
  alloc_free(state->attack_ratio);
  alloc_free(state->defense_ratio);

  alloc_free(state->pos_x);
  alloc_free(state->pos_y);
  alloc_free(state->pos_z);
  alloc_free(state->illusion_ghost_pos0_x);
  alloc_free(state->illusion_ghost_pos0_y);
  alloc_free(state->illusion_ghost_pos1_x);
  alloc_free(state->illusion_ghost_pos1_y);
  alloc_free(state->illusion_ghost_pos2_x);
  alloc_free(state->illusion_ghost_pos2_y);
  alloc_free(state->prev_pos_x);
  alloc_free(state->prev_pos_y);
  alloc_free(state->floor_sweep_prev_pos_x);
  alloc_free(state->floor_sweep_prev_pos_y);
  alloc_free(state->floor_sweep_seed_prev_pos_x);
  alloc_free(state->floor_sweep_seed_prev_pos_y);
  alloc_free(state->floor_sweep_seed_prev_valid);
  alloc_free(state->coll_ecb_bottom_rel_y);
  alloc_free(state->coll_prev_ecb_bottom_rel_y);
  alloc_free(state->coll_desired_ecb_bottom_rel_y);
  alloc_free(state->coll_ecb_bottom_valid);
  alloc_free(state->coll_prev_ecb_bottom_valid);
  alloc_free(state->coll_desired_ecb_bottom_valid);
  alloc_free(state->coll_stage_prev_pos_x);
  alloc_free(state->coll_stage_prev_pos_y);
  alloc_free(state->coll_stage_cur_pos_x);
  alloc_free(state->coll_stage_cur_pos_y);
  alloc_free(state->speed_air_x_self);
  alloc_free(state->speed_ground_x_self);
  alloc_free(state->speed_y_self);
  alloc_free(state->speed_x_attack);
  alloc_free(state->speed_y_attack);
  alloc_free(state->specialhi_rotate_model);
  alloc_free(state->specialhi_rotate_model_valid);
  alloc_free(state->fighter_scale_y);
  alloc_free(state->facing);
  alloc_free(state->facing_dir1);
  alloc_free(state->ground_friction_mul);
  alloc_free(state->kb_smashcharge_active);
  alloc_free(state->smash_charge_state);
  alloc_free(state->smash_charge_frames);
  alloc_free(state->smash_charge_hold_frames_max);
  alloc_free(state->smash_charge_saved_rate_fp_q16_16);
  alloc_free(state->on_ground);
  alloc_free(state->frame_start_on_ground);
  alloc_free(state->prev_on_ground);
  alloc_free(state->ground_contact_x);
  alloc_free(state->ground_contact_y);
  alloc_free(state->ground_normal_x);
  alloc_free(state->ground_normal_y);
  alloc_free(state->wall_contact_x);
  alloc_free(state->wall_contact_y);
  alloc_free(state->wall_normal_x);
  alloc_free(state->wall_normal_y);
  alloc_free(state->wall_id);
  alloc_free(state->wall_kind);
  alloc_free(state->damage_hitlag_wall_asdi_latch);
  alloc_free(state->ceiling_contact_x);
  alloc_free(state->ceiling_contact_y);
  alloc_free(state->ceiling_normal_x);
  alloc_free(state->ceiling_normal_y);
  alloc_free(state->ceiling_id);
  alloc_free(state->coll_env_flags);
  alloc_free(state->coll_prev_env_flags);

  alloc_free(state->action_id);
  alloc_free(state->seed_prev_action_id);
  alloc_free(state->seed_prev_action_frame);
  alloc_free(state->prev_action_id);
  alloc_free(state->prev_action_frame);
  alloc_free(state->action_frame);
  alloc_free(state->throw_pulse_consumed);
  alloc_free(state->throw_pulse_crossed_prev_frame);
  alloc_free(state->throw_command_pending_pulse_frame);
  alloc_free(state->throw_command_pending_seed_valid);
  alloc_free(state->throw_pulse_crossed_curr_frame);
  alloc_free(state->source_clear_timer_x18c8);
  alloc_free(state->source_clear_owner_set_phase);
  alloc_free(state->source_clear_processhit_damage_pending_phase);
  alloc_free(state->fighter_8006cda4_pre_gate_consume_count);
  alloc_free(state->source_clear_grounded_damage_clear_phase);
  alloc_free(state->source_clear_terminal_phase);
  alloc_free(state->throw_pending_victim_port);
  alloc_free(state->throw_pending_hit_idx);
  alloc_free(state->attached_victim_port);
  alloc_free(state->grab_owner_port);
  alloc_free(state->grab_mash_stick_x_sign);
  alloc_free(state->grab_mash_stick_y_sign);
  alloc_free(state->grab_offset_y);
  alloc_free(state->grab_offset_z);
  alloc_free(state->thrown_attached_prev_on_ground);
  alloc_free(state->thrown_attached_prev_ground_id);
  alloc_free(state->match_flow_timer);
  alloc_free(state->entry_end_fall_lock);
  alloc_free(state->camera_box_visible_x221f_b0);
  alloc_free(state->rebirth_camera_anchor_y_f32);
  alloc_free(state->camera_target_world_x_f32);
  alloc_free(state->camera_target_world_y_f32);
  alloc_free(state->camera_target_world_z_f32);
  alloc_free(state->camera_box_radius_f32);
  alloc_free(state->camera_target_point_inside_stage_cam_bounds_u8);
  alloc_free(state->magnify_damage_counter_x1910);
  alloc_free(state->downwait_timer);
  alloc_free(state->passivewall_timer);
  alloc_free(state->walljump_input_timer);
  alloc_free(state->walljump_wall_side_i8);
  alloc_free(state->walljump_seed_phase_valid);
  alloc_free(state->anim_frame_f32);
  alloc_free(state->anim_frame_fp_q16_16);
  alloc_free(state->frame_speed_mul_fp_q16_16);
  alloc_free(state->walk_anim_source_vel);
  alloc_free(state->walk_retarget_tick_source_vel);
  alloc_free(state->run_anim_source_vel);
  alloc_free(state->rebound_ground_accel_2);
  alloc_free(state->rebound_anim_rate_fp_q16_16);
  alloc_free(state->turn_kneebend_facing_override);
  alloc_free(state->capture_grab_timer);
  alloc_free(state->capture_wait_counter);
  alloc_free(state->capture_wait_anim_rate_timer);
  alloc_free(state->capture_wait_jump_latch);
  alloc_free(state->capture_breakout_pending);
  alloc_free(state->throw_anim_rate_fp_q16_16);
  alloc_free(state->anim_defer_tick_once);
  alloc_free(state->jumps_left);
  alloc_free(state->stocks);
  alloc_free(state->guard_tilt_x8);
  alloc_free(state->guard_tilt_x4);
  alloc_free(state->guard_on_entered_this_frame);
  alloc_free(state->guard_entry_via_wait_callback);
  alloc_free(state->guard_entry_via_dash_91ad8);
  alloc_free(state->guard_x10_frame_start);
  alloc_free(state->guard_reflect_entry_dash_terminal_scalar);
  alloc_free(state->guard_seed_shield_desc_active);
  alloc_free(state->state_flags_2218_frame_start);
  alloc_free(state->guard_jump_oos_entered_this_frame);
  alloc_free(state->shine_jump_iasa_entered_this_frame);
  alloc_free(state->guard_reflect_timer_x14);
  alloc_free(state->guard_reflect_timer_x18);
  alloc_free(state->guard_reflect_timer_x14_seed);
  alloc_free(state->guard_reflect_timer_x18_seed);
  alloc_free(state->guard_reflect_origin_guardon);
  alloc_free(state->guard_special_enable_timer_x1c);
  alloc_free(state->guard_release_latched_xc);
  alloc_free(state->guard_x10);
  alloc_free(state->lightshield_amount);
  alloc_free(state->guard_setoff_hitlag_damage_min);
  alloc_free(state->guard_setoff_hitlag_exit_phase_u8);
  alloc_free(state->guard_setoff_post_hitlag_owner_u8);
  alloc_free(state->kneebend_jump_input);
  alloc_free(state->kneebend_is_short_hop);
  alloc_free(state->tilt_timer_x);
  alloc_free(state->tilt_timer_y);
  alloc_free(state->fall_fast);
  alloc_free(state->attackdash_x0);
  alloc_free(state->jab_x0);
  alloc_free(state->jab_rapid_count);
  alloc_free(state->attack100_x0);
  alloc_free(state->attack100_x4);
  alloc_free(state->run_x0);
  alloc_free(state->runbrake_cmd0);
  alloc_free(state->dash_x4);
  alloc_free(state->shine_release_lag);
  alloc_free(state->shine_is_release);
  alloc_free(state->ecb_lock_timer);
  alloc_free(state->ledge_side);
  alloc_free(state->stage_ledge_occupant_left);
  alloc_free(state->stage_ledge_occupant_right);
  alloc_free(state->ledge_cooldown);
  alloc_free(state->fallspecial_xc);
  alloc_free(state->fallspecial_landing_lag);
  alloc_free(state->landing_fallspecial_allow_interrupt);
  alloc_free(state->turn_has_turned);
  alloc_free(state->turn_frames_to_turn);
  alloc_free(state->walk_use_raw_input_once);
  alloc_free(state->turn_x8);
  alloc_free(state->lr_press_timer);
  alloc_free(state->x672_input_timer);
  alloc_free(state->x673);
  alloc_free(state->x674);
  alloc_free(state->x675);
  alloc_free(state->x676_x);
  alloc_free(state->x2228_b7);
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
  alloc_free(state->percent_temp);
  alloc_free(state->phantom_damage_pending_x1898);
  alloc_free(state->phantom_damage_timer_x189c);
  alloc_free(state->phantom_damage_source_port);
  alloc_free(state->damage_time_since_hit_x18ac);
  alloc_free(state->dmg_x2225_b7);
  alloc_free(state->dmg_x2224_b2);
  alloc_free(state->shield_hp);
  alloc_free(state->hitlag);
  alloc_free(state->hitlag_pre_timer);
  alloc_free(state->hitlag_started_frame);
  alloc_free(state->damage_hitlag_floorhug_latch);
  alloc_free(state->damage_hitlag_downward_sdi_consumed);
  alloc_free(state->hitstun);
  alloc_free(state->damage_jump_buffer_x14);
  alloc_free(state->damage_post_hitlag_cb_kind);
  alloc_free(state->attacker_shield_ground_kb_vel);
  alloc_free(state->l_cancel);
  alloc_free(state->hurtbox_state);
  alloc_free(state->colanim_hit_status_x198c);
  alloc_free(state->colanim_timer_x1990);
  alloc_free(state->colanim_timer_x1994);
  alloc_free(state->colanim_lock_x2221_b0);
  alloc_free(state->colanim_hitstun_x198c1_seed);
  alloc_free(state->colanim_terminal_x1990_item_body_guard);
  alloc_free(state->hurtcap_count);
  alloc_free(state->hurtcap_geometry_valid);
  alloc_free(state->hurtcap_a_x);
  alloc_free(state->hurtcap_a_y);
  alloc_free(state->hurtcap_a_z);
  alloc_free(state->hurtcap_b_x);
  alloc_free(state->hurtcap_b_y);
  alloc_free(state->hurtcap_b_z);
  alloc_free(state->hurtcap_radius);
  alloc_free(state->hurtcap_enabled);
  alloc_free(state->hurtcap_is_grabbable);
  alloc_free(state->hurtcap_height);
  alloc_free(state->hitbox_count);
  alloc_free(state->hitbox_enabled);
  alloc_free(state->hitbox_prev_enabled);
  alloc_free(state->hitbox_prev_x);
  alloc_free(state->hitbox_prev_y);
  alloc_free(state->hitbox_prev_z);
  alloc_free(state->hitbox_pose_create);
  alloc_free(state->hitbox_enable_edge);
  alloc_free(state->hitbox_x43_b2);
  alloc_free(state->hitbox_prev_bootstrap);
  alloc_free(state->hitbox_x);
  alloc_free(state->hitbox_y);
  alloc_free(state->hitbox_z);
  alloc_free(state->hitbox_radius);
  alloc_free(state->hitbox_damage);
  alloc_free(state->hitbox_bone_part_id);
  alloc_free(state->hitbox_u16_0);
  alloc_free(state->hitbox_u16_1);
  alloc_free(state->hitbox_u16_2);
  alloc_free(state->hitbox_u16_3);
  alloc_free(state->hitbox_u16_4);
  alloc_free(state->hitbox_u16_5);
  alloc_free(state->hitbox_u16_6);
  alloc_free(state->hitbox_u16_7);
  alloc_free(state->hitbox_angle);
  alloc_free(state->hitbox_kbg);
  alloc_free(state->hitbox_wsk);
  alloc_free(state->hitbox_bkb);
  alloc_free(state->hitbox_element);
  alloc_free(state->hitbox_shield_damage);
  alloc_free(state->hitbox_sfx_severity);
  alloc_free(state->hitbox_sfx_kind);
  alloc_free(state->hitbox_flags);
  alloc_free(state->shield_x);
  alloc_free(state->shield_y);
  alloc_free(state->shield_z);
  alloc_free(state->shield_radius);
  alloc_free(state->reflector_x);
  alloc_free(state->reflector_y);
  alloc_free(state->reflector_radius);
  alloc_free(state->ground_id);
  alloc_free(state->floor_skip_segment_id);
  alloc_free(state->animation_index);
  alloc_free(state->dynamic_pose_state_valid);
  alloc_free(state->dynamic_pose_apply_collision_matrix);
  alloc_free(state->dynamic_pose_node_count);
  alloc_free(state->dynamic_pose_char_id);
  alloc_free(state->dynamic_pose_msid);
  alloc_free(state->dynamic_pose_frame);
  alloc_free(state->dynamic_pose_rot_x);
  alloc_free(state->dynamic_pose_rot_y);
  alloc_free(state->dynamic_pose_rot_z);
  alloc_free(state->dynamic_pose_pos_x);
  alloc_free(state->dynamic_pose_pos_y);
  alloc_free(state->dynamic_pose_pos_z);
  alloc_free(state->dynamic_pose_axis_x);
  alloc_free(state->dynamic_pose_axis_y);
  alloc_free(state->dynamic_pose_axis_z);
  alloc_free(state->dynamic_pose_angle);
  alloc_free(state->instance_hit_by);
  alloc_free(state->instance_id);
  alloc_free(state->instance_id_x2073);
  alloc_free(state->motion_entry_instance_id_override);
  alloc_free(state->instance_identity_last_action_id);
  alloc_free(state->attack_id);
  alloc_free(state->attack_instance);
  alloc_free(state->attack_identity_last_action_id);
  alloc_free(state->last_attack_landed);
  alloc_free(state->combo_count);
  alloc_free(state->combo_victim_port);
  alloc_free(state->combo_victim_instance_id);
  alloc_free(state->combo_timer_x2098);
  alloc_free(state->combo_push_timer_x2092);
  alloc_free(state->source_port0);
  alloc_free(state->last_hit_by);
  alloc_free(state->state_flags);
  alloc_free(state->combat_hitlist_cd);
  alloc_free(state->combat_hitlist_victim_iid);
  alloc_free(state->combat_hitlist_hb_valid);
  alloc_free(state->combat_hitlist_hb_cd);
  alloc_free(state->combat_hitlist_hb_victim_iid);
  alloc_free(state->combat_shield_contact_hb_kind);
  alloc_free(state->combat_shield_hit_int_damage);
  alloc_free(state->combat_shield_damage_taken);
  alloc_free(state->hitlist_reseed_gen);
  alloc_free(state->fighter_hitlist);
  alloc_free(state->fighter_hitlist_init_gen);
  alloc_free(state->stale_queue_index);
  alloc_free(state->stale_move_id);
  alloc_free(state->stale_attack_instance);

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
  alloc_free(state->prev_input_c_x);
  alloc_free(state->prev_input_c_y);
  alloc_free(state->prev_input_l);
  alloc_free(state->prev_input_r);
  alloc_free(state->input_l);
  alloc_free(state->input_r);

  alloc_free(state->item_exists);
  alloc_free(state->item_state);
  alloc_free(state->item_type);
  alloc_free(state->item_owner);
  alloc_free(state->item_instance_id);
  alloc_free(state->item_attack_id);
  alloc_free(state->item_attack_instance);
  alloc_free(state->item_direction);
  alloc_free(state->item_vel_x);
  alloc_free(state->item_vel_y);
  alloc_free(state->item_pos_x);
  alloc_free(state->item_pos_y);
  alloc_free(state->item_damage);
  alloc_free(state->item_reflect_damage_mul);
  alloc_free(state->item_timer);
  alloc_free(state->item_hitlag);
  alloc_free(state->item_spawn_id);
  alloc_free(state->item_misc0);
  alloc_free(state->item_misc1);
  alloc_free(state->item_misc2);
  alloc_free(state->item_misc3);
  alloc_free(state->item_pending_reflect_owner_port);
  alloc_free(state->item_pending_reflect_instance_id);
  alloc_free(state->item_reflect_transfer_seed_port);
  alloc_free(state->item_reflect_transfer_seed_iid);
  alloc_free(state->item_shield_bounce_seed_valid);
  alloc_free(state->item_shield_bounce_seed_vel_x);
  alloc_free(state->item_shield_bounce_seed_vel_y);
  alloc_free(state->item_hidden_body_hit_victim_port);
  alloc_free(state->item_hidden_body_hit_hurt_height);
  alloc_free(state->item_hidden_callback_flags);
  alloc_free(state->item_shyguy_prev_vel_y);
  alloc_free(state->item_shyguy_prev_vel_y_valid);
  alloc_free(state->item_shyguy_dyn_y_phase);
  alloc_free(state->item_shyguy_dyn_y_phase_valid);
  alloc_free(state->item_shyguy_speed_index);
  alloc_free(state->item_shyguy_speed_index_valid);
  alloc_free(state->item_shyguy_delay);
  alloc_free(state->item_shyguy_delay_valid);
  alloc_free(state->item_shyguy_hitlag);
  alloc_free(state->item_shyguy_hitlag_valid);
  alloc_free(state->item_hitlist);

  state_zero_ptrs(state);
}
