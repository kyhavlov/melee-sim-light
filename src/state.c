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

#define MSL_STATE_FIELD(field, element_type, allocation_elements, elements_per_lane) \
  state->field = (element_type*)alloc_aligned_64(sizeof(element_type) * (allocation_elements));
#include "state_fields.inc"
#undef MSL_STATE_FIELD

#define MSL_STATE_FIELD(field, element_type, allocation_elements, elements_per_lane) \
  if (state->field == NULL) {                                                        \
    return -1;                                                                       \
  }
#include "state_fields.inc"
#undef MSL_STATE_FIELD

  // Initialize hitlists to a known-empty state (entry.kind_slot == 0xFF).
  for (size_t i = 0; i < b; i++) {
    state->hitlist_reseed_gen[i] = 1u;
  }
  memset(state->frame_id, 0, sizeof(int32_t) * b);
  memset(state->stage_fod_platform_scheduler_phase, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_height_source, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_deferred_velocity, 0, sizeof(float) * b2);
  memset(state->stage_fod_platform_deferred_velocity_valid, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_scheduler_timer, 0, sizeof(uint16_t) * b2);
  memset(state->stage_fod_platform_scheduler_target, 0, sizeof(float) * b2);
  memset(state->stage_fod_platform_scheduler_wait_origin, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_scheduler_next_frame_rng, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_scheduler_valid, 0, sizeof(uint8_t) * b2);
  memset(state->stage_fod_platform_visible_choice_timer, 0, sizeof(uint16_t) * b2);
  memset(state->stage_fod_platform_visible_choice_rng_seed, 0, sizeof(uint32_t) * b2);
  memset(state->stage_fod_platform_visible_choice_valid, 0, sizeof(uint8_t) * b2);
  memset(state->stage_yoshi_shyguy_spawn_rng_seed, 0, sizeof(uint32_t) * b);
  memset(state->stage_yoshi_shyguy_spawn_rng_valid, 0, sizeof(uint8_t) * b);
  memset(state->stage_dream_whispy_wind_dir, 0, sizeof(uint8_t) * b);
  memset(state->stage_dream_whispy_wind_valid, 0, sizeof(uint8_t) * b);
  memset(state->stage_dream_whispy_wind_timer, 0, sizeof(uint16_t) * b);
  memset(state->item_spawn_id_counter, 0, sizeof(uint32_t) * b);
  memset(state->dynamic_pose_state_valid, 0, sizeof(uint8_t) * bp);
  memset(state->camera_target_live_pose_valid, 0, sizeof(uint8_t) * bp);
  memset(state->camera_box_visible_x221f_b0_replay_rise, 0, sizeof(uint8_t) * bp);
  memset(state->camera_box_visible_x221f_b0_replay_prev, 0, sizeof(uint8_t) * bp);
  memset(state->magnify_damage_runtime_visibility_owner, 0, sizeof(uint8_t) * bp);
  memset(state->magnify_damage_seed_episode_active, 0, sizeof(uint8_t) * bp);
  memset(state->magnify_damage_local_episode_kind, 0, sizeof(uint8_t) * bp);
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
  memset(state->guard_on_cliff_end_source, 0, sizeof(uint8_t) * bp);
  memset(state->guard_on_entry_reflect_source_latch, 0, sizeof(uint8_t) * bp);
  memset(state->guard_reflect_entry_dash_terminal_scalar, 0, sizeof(uint8_t) * bp);
  memset(state->guard_seed_shield_desc_active, 0, sizeof(uint8_t) * bp);
  memset(state->guard_special_enable_timer_x1c, 0, sizeof(uint8_t) * bp);
  memset(state->throw_anim_rate_fp_q16_16, 0, sizeof(int32_t) * bp);
  memset(state->throw_command_pending_pulse_frame, 0, sizeof(uint8_t) * bp);
  memset(state->throw_command_pending_seed_valid, 0, sizeof(uint8_t) * bp);
  memset(state->throw_command_deferred_pulse_frame, 0, sizeof(uint8_t) * bp);
  memset(state->throw_pulse_crossed_curr_frame, 0, sizeof(uint8_t) * bp);
  memset(state->dead_up_fall_offset_x, 0, sizeof(float) * bp);
  memset(state->dead_up_fall_offset_y, 0, sizeof(float) * bp);
  memset(state->dead_up_fall_offset_z, 0, sizeof(float) * bp);
  memset(state->dead_up_fall_vel_x, 0, sizeof(float) * bp);
  memset(state->dead_up_fall_vel_y, 0, sizeof(float) * bp);
  memset(state->dead_up_fall_vel_z, 0, sizeof(float) * bp);
  memset(state->match_flow_pending_rebirth_char_id, 0, sizeof(uint8_t) * bp);
  memset(state->magnify_damage_counter_x1910, 0, sizeof(uint16_t) * bp);
  memset(state->passivewall_jump_latch, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_state, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_frames, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_hold_frames_max, 0, sizeof(uint8_t) * bp);
  memset(state->smash_charge_saved_rate_fp_q16_16, 0, sizeof(int32_t) * bp);
  memset(state->walljump_seed_phase_valid, 0, sizeof(uint8_t) * bp);
  memset(state->specialhi_rotate_model, 0, sizeof(float) * bp);
  memset(state->specialhi_rotate_model_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_ecb_top_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_ecb_left_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_ecb_right_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_ecb_side_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_top_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_left_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_right_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_prev_ecb_side_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_squeeze_restore_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_squeeze_restore_ecb_top_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_squeeze_restore_ecb_left_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_squeeze_restore_ecb_right_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_squeeze_restore_ecb_side_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_bottom_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_top_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_left_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_right_rel_x, 0, sizeof(float) * bp);
  memset(state->coll_desired_ecb_side_rel_y, 0, sizeof(float) * bp);
  memset(state->coll_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_prev_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_squeeze_restore_ecb_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_desired_ecb_bottom_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_desired_ecb_bottom_locked_owner, 0, sizeof(uint8_t) * bp);
  memset(state->coll_common_fall_blended_ecb_seed_valid, 0, sizeof(uint8_t) * bp);
  memset(state->floor_sweep_prev_runtime_owned, 0, sizeof(uint8_t) * bp);
  memset(state->coll_damage_hitlag_floor_contact_runtime, 0, sizeof(uint8_t) * bp);
  memset(state->coll_escapeair_floor_producer_runtime, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_result_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_result_source, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_result_segment_id, 0xFF, sizeof(uint16_t) * bp);
  memset(state->coll_stage_prev_ground_id, 0xFF, sizeof(uint16_t) * bp);
  memset(state->coll_floor_result_contact_x, 0, sizeof(float) * bp);
  memset(state->coll_floor_result_contact_y, 0, sizeof(float) * bp);
  memset(state->coll_floor_result_normal_x, 0, sizeof(float) * bp);
  memset(state->coll_floor_result_normal_y, 0, sizeof(float) * bp);
  memset(state->coll_floor_probe_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_owner, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_reject_reason, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_raw_bottom_sweep_hit, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_projection_hit, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_carried_source_owned, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_carried_runtime_owned, 0, sizeof(uint8_t) * bp);
  memset(state->coll_floor_probe_reject_bits, 0, sizeof(uint64_t) * bp);
  memset(state->coll_floor_probe_source_phases, 0, sizeof(uint32_t) * bp);
  memset(state->coll_floor_probe_carried_segment_id, 0xFF, sizeof(uint16_t) * bp);
  memset(state->coll_floor_probe_candidate_segment_id, 0xFF, sizeof(uint16_t) * bp);
  memset(state->coll_floor_probe_projected_segment_id, 0xFF, sizeof(uint16_t) * bp);
  memset(state->coll_floor_probe_candidate_line_idx, 0xFF, sizeof(int16_t) * bp);
  memset(state->coll_wall_probe_valid, 0, sizeof(uint8_t) * bp);
  memset(state->coll_wall_probe_side, 0, sizeof(uint8_t) * bp);
  memset(state->coll_wall_probe_commit_kind, 0, sizeof(uint8_t) * bp);
  memset(state->coll_wall_probe_candidate_count, 0, sizeof(uint8_t) * bp);
  memset(state->coll_wall_probe_segment_id, 0xFF, sizeof(int16_t) * bp);
  memset(state->coll_wall_probe_corr_x, 0, sizeof(float) * bp);
  memset(state->coll_floor_probe_projected_line_idx, 0xFF, sizeof(int16_t) * bp);
  memset(state->coll_floor_probe_prev_bottom_x, 0, sizeof(float) * bp);
  memset(state->coll_floor_probe_prev_bottom_y, 0, sizeof(float) * bp);
  memset(state->coll_floor_probe_cur_bottom_x, 0, sizeof(float) * bp);
  memset(state->coll_floor_probe_cur_bottom_y, 0, sizeof(float) * bp);
  memset(state->coll_substep_prev_pos_x, 0, sizeof(float) * bp);
  memset(state->coll_substep_prev_pos_y, 0, sizeof(float) * bp);
  memset(state->coll_substep_cur_pos_x, 0, sizeof(float) * bp);
  memset(state->coll_substep_cur_pos_y, 0, sizeof(float) * bp);
  memset(state->coll_last_pos_x, 0, sizeof(float) * bp);
  memset(state->coll_last_pos_y, 0, sizeof(float) * bp);
  memset(state->walk_anim_source_vel, 0, sizeof(float) * bp);
  memset(state->walk_retarget_tick_source_vel, 0, sizeof(float) * bp);
  memset(state->run_anim_source_vel, 0, sizeof(float) * bp);
  memset(state->turn_kneebend_facing_override, 0, sizeof(uint8_t) * bp);
  memset(state->fall_fast_frame_start, 0, sizeof(uint8_t) * bp);
  memset(state->fall_fast_seed_frame_start, 0, sizeof(uint8_t) * bp);
  memset(state->fall_fast_seed_frame_start_valid, 0, sizeof(uint8_t) * bp);
  memset(state->common_fall_blend_x4, 0, sizeof(float) * bp);
  memset(state->common_fall_blend_msid, 0, sizeof(uint16_t) * bp);
  memset(state->squat_pass_x0, 0, sizeof(uint8_t) * bp);
  memset(state->squat_pass_x4, 0, sizeof(uint8_t) * bp);
  memset(state->sheik_needle_count, 0, sizeof(uint8_t) * bp);
  memset(state->sheik_special_timer, 0, sizeof(uint8_t) * bp);
  memset(state->sheik_special_timer_frame_start, 0, sizeof(uint8_t) * bp);
  memset(state->sheik_special_latch, 0, sizeof(uint8_t) * bp);
  memset(state->zelda_twin_state_flags_2218, 0, sizeof(uint8_t) * bp);
  memset(state->walk_use_raw_input_once, 0, sizeof(uint8_t) * bp);
  memset(state->x2228_b7, 0, sizeof(uint8_t) * bp);
  memset(state->fallspecial_landing_lag, 0, sizeof(float) * bp);
  memset(state->damage_hitlag_wall_asdi_latch, 0, sizeof(uint8_t) * bp);
  memset(state->damage_allow_sdi, 0, sizeof(uint8_t) * bp);
  memset(state->damage_hitlag_floorhug_latch, 0, sizeof(uint8_t) * bp);
  memset(state->damage_hitlag_downward_sdi_consumed, 0, sizeof(uint8_t) * bp);
  memset(state->damage_meteor_cancel_eligible_x1a, 0, sizeof(uint8_t) * bp);
  memset(state->phantom_damage_pending_x1898, 0, sizeof(float) * bp);
  memset(state->phantom_damage_timer_x189c, 0, sizeof(uint16_t) * bp);
  memset(state->phantom_damage_source_port, 0xFF, sizeof(uint8_t) * bp);
  for (size_t i = 0; i < bp; i++) {
    state->floor_skip_segment_id[i] = 0xFFFFu;
    state->ledge_drop_floor_skip_segment_id[i] = 0xFFFFu;
    state->cliff_ledge_floor_segment_id[i] = 0xFFFFu;
    state->cliff_ledge_floor_segment_seeded[i] = 0u;
    state->mpcoll_joint_id_skip[i] = -1;
    state->mpcoll_joint_id_only[i] = -1;
  }
  for (size_t i = 0; i < bph; i++) {
    state->hitbox_capsule_enabled[i] = 0u;
    state->hitbox_capsule_group[i] = 0u;
    state->hitbox_stale_damage_valid[i] = 0u;
    state->hitbox_stale_damage_mul[i] = 1.0f;
    state->fighter_hitlist_init_gen[i] = 0u;
    hitlist_capsule_clear(&state->fighter_hitlist[i]);
  }
  for (size_t i = 0; i < bi; i++) {
    state->item_stale_damage_valid[i] = 0u;
    state->item_stale_damage_mul[i] = 1.0f;
    state->item_sheik_chain_stale_damage_valid[i] = 0u;
    state->item_sheik_chain_stale_damage_mul[i] = 1.0f;
    state->item_hitlag[i] = 0u;
  }
  for (size_t i = 0; i < bi * (size_t)MSL_MAX_HITBOXES; i++) {
    hitlist_capsule_clear(&state->item_hitlist[i]);
  }

  return 0;
}

static int state_validate_lanes(const int32_t* lanes, int32_t count, int32_t batch_size) {
  if (lanes == NULL || count < 0 || batch_size <= 0) {
    return -1;
  }
  for (int32_t i = 0; i < count; i++) {
    if (lanes[i] < 0 || lanes[i] >= batch_size) {
      return -1;
    }
  }
  return 0;
}

static void state_copy_one_lane(MslStateSoA* dst, const MslStateSoA* src, int32_t dst_lane,
                                int32_t src_lane) {
#define MSL_STATE_FIELD(field, element_type, allocation_elements, elements_per_lane) \
  do {                                                                               \
    const size_t n = (size_t)(elements_per_lane);                                    \
    memmove(&dst->field[(size_t)dst_lane * n], &src->field[(size_t)src_lane * n],    \
            sizeof(element_type) * n);                                               \
  } while (0);
#include "state_fields.inc"
#undef MSL_STATE_FIELD
}

int state_copy_lanes(MslStateSoA* dst, const MslStateSoA* src, const int32_t* dst_lanes,
                     const int32_t* src_lanes, int32_t count, int32_t dst_batch_size,
                     int32_t src_batch_size) {
  if (dst == NULL || src == NULL || state_validate_lanes(dst_lanes, count, dst_batch_size) != 0 ||
      state_validate_lanes(src_lanes, count, src_batch_size) != 0) {
    return -1;
  }
  for (int32_t i = 0; i < count; i++) {
    state_copy_one_lane(dst, src, dst_lanes[i], src_lanes[i]);
  }
  return 0;
}

void state_free(MslStateSoA* state) {
  if (state == NULL) {
    return;
  }
#define MSL_STATE_FIELD(field, element_type, allocation_elements, elements_per_lane) \
  alloc_free(state->field);
#include "state_fields.inc"
#undef MSL_STATE_FIELD

  state_zero_ptrs(state);
}
