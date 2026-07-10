#define MSL_BINDING_IMPORT_ARRAY
#include "msl_binding_internal.h"
#include "msl_binding_data.h"
#include "msl_binding_debug.h"
#include "msl_binding_disruptive.h"
#include "msl_binding_runtime.h"
#include "msl_binding_validation.h"
#include "msl_preprocess.h"
#include "msl_taxonomy_native.h"
#include "msl_validation_buffers.h"
#include "msl_validation_combat.h"
#include "msl_validation_damage_history.h"
#include "msl_validation_input.h"
#include "msl_validation_movement_history.h"
#include "msl_validation_items.h"
#include "msl_validation_specials.h"
#include "msl_validation_stage.h"

static PyMethodDef methods[] = {
    {"init", (PyCFunction)(void (*)(void))msl_init, METH_VARARGS | METH_KEYWORDS,
     "init(batch_size, num_players, ucf_enabled=?, ucf_cardinals_1_0_enabled=?) -> handle"},
    {"destroy", msl_destroy, METH_VARARGS,
     "destroy(handle) -> None (free underlying C batch immediately)"},
    {"reseed_seed", msl_reseed_seed, METH_VARARGS,
     "reseed_seed(handle, seed_bytes[batch, seed_stride])"},
    {"reseed_seed_rollout", msl_reseed_seed_rollout, METH_VARARGS,
     "reseed_seed_rollout(handle, seed_bytes[batch, seed_stride])"},
    {"apply_replay_frame_rng", msl_apply_replay_frame_rng, METH_VARARGS,
     "apply_replay_frame_rng(handle, seed_bytes[batch, seed_stride])"},
    {"init_match", msl_init_match, METH_VARARGS,
     "init_match(handle, match_config_bytes[batch, match_config_stride])"},
    {"init_match_masked", msl_init_match_masked, METH_VARARGS,
     "init_match_masked(handle, match_config_bytes[batch, match_config_stride], mask[batch])"},
    {"debug_copy_lanes", msl_debug_copy_lanes_py, METH_VARARGS,
     "debug_copy_lanes(dst_handle, src_handle, dst_lanes[int32], src_lanes[int32]) -> "
     "DEBUG-ONLY. Exercise msl_batch_copy_lanes for focused API tests."},
    {"debug_copy_colldata", msl_debug_copy_colldata_py, METH_VARARGS,
     "debug_copy_colldata(handle, batch_index, src_player, dst_player) -> DEBUG-ONLY. Copy modeled "
     "CollData lanes without routing action owners."},
    {"step_input", msl_step_input, METH_VARARGS,
     "step_input(handle, prev_input_bytes, input_bytes)"},
    {"step_input_replay_frame_rng", msl_step_input_replay_frame_rng, METH_VARARGS,
     "step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)"},
    {"debug_step_input_pre_combat", msl_debug_step_input_pre_combat, METH_VARARGS,
     "debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes) -> DEBUG-ONLY triage "
     "step. Advances/mutates state through pre-combat stages, deliberately skips combat_resolve(), "
     "and is not comparable to step_input() for training/rollouts."},
    {"debug_knockdown_update_pre_physics", msl_debug_knockdown_update_pre_physics, METH_VARARGS,
     "debug_knockdown_update_pre_physics(handle) -> DEBUG-ONLY branch isolation. Runs only the "
     "knockdown/damage pre-physics callback slice on the current reseeded state; not for "
     "training/rollouts."},
    {"debug_set_coll_env_flags", msl_debug_set_coll_env_flags_py, METH_VARARGS,
     "debug_set_coll_env_flags(handle, batch_index, player_index, flags) -> DEBUG-ONLY. Override "
     "coll_env_flags for a single fighter."},
    {"debug_set_mpcoll_joint_filters", msl_debug_set_mpcoll_joint_filters_py, METH_VARARGS,
     "debug_set_mpcoll_joint_filters(handle, batch_index, player_index, joint_skip, joint_only) -> "
     "DEBUG-ONLY. Override CollData joint filters (-1 disables each)."},
    {"debug_set_escapeair_floor_producer_runtime",
     msl_debug_set_escapeair_floor_producer_runtime_py, METH_VARARGS,
     "debug_set_escapeair_floor_producer_runtime(handle, batch_index, player_index, authority, "
     "desired_owner) -> DEBUG-ONLY. Override EscapeAir floor-producer authority for tests."},
    {"debug_set_floor_sweep_prev_runtime", msl_debug_set_floor_sweep_prev_runtime_py, METH_VARARGS,
     "debug_set_floor_sweep_prev_runtime(handle, batch_index, player_index, x, y, authority) -> "
     "DEBUG-ONLY. Override runtime floor-sweep provenance for tests."},
    {"debug_set_wall_ceil_prev_runtime", msl_debug_set_wall_ceil_prev_runtime_py, METH_VARARGS,
     "debug_set_wall_ceil_prev_runtime(handle, batch_index, player_index, x, y, authority) -> "
     "DEBUG-ONLY. Override runtime wall/ceiling previous-root provenance for tests."},
    {"debug_set_player_root", msl_debug_set_player_root_py, METH_VARARGS,
     "debug_set_player_root(handle, batch_index, player_index, pos_x, pos_y, facing) -> "
     "DEBUG-ONLY. Override fighter root position/facing for fixture setup."},
    {"debug_set_ceiling_contact", msl_debug_set_ceiling_contact_py, METH_VARARGS,
     "debug_set_ceiling_contact(handle, batch_index, player_index, contact_y) -> DEBUG-ONLY. "
     "Override ceiling_contact_y for a single fighter."},
    {"debug_run_knockdown_post_collision", msl_debug_run_knockdown_post_collision_py, METH_VARARGS,
     "debug_run_knockdown_post_collision(handle) -> DEBUG-ONLY. Re-run "
     "knockdown_update_post_collision on current batch state."},
    {"debug_run_locomotion_post_collision", msl_debug_run_locomotion_post_collision_py,
     METH_VARARGS,
     "debug_run_locomotion_post_collision(handle) -> DEBUG-ONLY. Re-run "
     "locomotion_update_post_collision on current batch state."},
    {"debug_refresh_combat_geometry", msl_debug_refresh_combat_geometry, METH_VARARGS,
     "debug_refresh_combat_geometry(handle) -> DEBUG-ONLY. Recompute hurtcaps/hitboxes from "
     "current state without advancing frame stages."},
    {"write_compare", msl_write_compare, METH_VARARGS, "write_compare(handle, out_bytes)"},
    {"bind_buffers", msl_bind_buffers, METH_VARARGS,
     "bind_buffers(handle, match_config, prev_input, input, compare[, viewpoint, "
     "gamestate, terminal])"},
    {"unbind_buffers", msl_unbind_buffers, METH_VARARGS, "unbind_buffers(handle)"},
    {"bind_sequence_buffers", msl_bind_sequence_buffers, METH_VARARGS,
     "bind_sequence_buffers(handle, match_config, action, compare, viewpoint, gamestate, "
     "terminal, done, reset_mask, action_format)"},
    {"unbind_sequence_buffers", msl_unbind_sequence_buffers, METH_VARARGS,
     "unbind_sequence_buffers(handle)"},
    {"init_match_bound", msl_init_match_bound, METH_VARARGS,
     "init_match_bound(handle) -> initialize from bound match_config"},
    {"init_match_sequence_bound", msl_init_match_sequence_bound, METH_VARARGS,
     "init_match_sequence_bound(handle) -> initialize from bound sequence match_config"},
    {"reset_sequence_masked", msl_reset_sequence_masked, METH_VARARGS,
     "reset_sequence_masked(handle, frame, write_initial_observation=True)"},
    {"reset_prev_input", msl_reset_prev_input, METH_VARARGS,
     "reset_prev_input(handle) -> clear env-owned previous input storage"},
    {"set_prev_input_from_sequence", msl_set_prev_input_from_sequence, METH_VARARGS,
     "set_prev_input_from_sequence(handle, frame) -> copy sequence action[frame] to previous "
     "input"},
    {"step_bound", msl_step_bound, METH_VARARGS,
     "step_bound(handle) -> step using bound prev_input/input buffers"},
    {"step_sequence", msl_step_sequence, METH_VARARGS,
     "step_sequence(handle, frame, write_outputs=True, write_compare=False, max_frame_id=-1)"},
    {"write_compare_bound", msl_write_compare_bound, METH_VARARGS,
     "write_compare_bound(handle) -> write bound compare buffer"},
    {"step_write_compare_bound", msl_step_write_compare_bound, METH_VARARGS,
     "step_write_compare_bound(handle) -> fused step + compare write using bound buffers"},
    {"write_gamestate_bound", msl_write_gamestate_bound, METH_VARARGS,
     "write_gamestate_bound(handle) -> write bound gamestate buffer"},
    {"write_terminal_bound", msl_write_terminal_bound, METH_VARARGS,
     "write_terminal_bound(handle, max_frame_id=-1) -> write bound terminal buffer"},
    {"collect_mismatch_events", msl_collect_mismatch_events, METH_VARARGS,
     "collect_mismatch_events(seed_bytes, ref_bytes, out_bytes, num_players) -> dict[np.ndarray]. "
     "Native strict-compare event scanner."},
    {"disruptive_scan", msl_disruptive_scan, METH_VARARGS,
     "disruptive_scan(samples_u8, horizons, discrete_fields, float_fields, players, num_players, "
     "max_records, stride, float_epsilon, ucf_enabled, ucf_cardinals_enabled, batch_size, "
     "start_record, stop_record, profile_rl1) -> (int64[:,22], float64[:,6])"},
    {"slpz_unorder_events", msl_slpz_unorder_events, METH_VARARGS,
     "slpz_unorder_events(data, sizes) -> bytes. Native .slpz event payload unshuffle."},
    {"standard_rollout_compare", msl_standard_rollout_compare, METH_VARARGS,
     "standard_rollout_compare(out_compare, ref_compare, players_u8, profile_rl1) -> int code"},
    {"one_step_summary_create", msl_one_step_summary_create, METH_VARARGS,
     "one_step_summary_create(total_records, num_players, profile_rl1) -> native accumulator"},
    {"one_step_summary_accumulate", msl_one_step_summary_accumulate, METH_VARARGS,
     "one_step_summary_accumulate(summary, out_compare_u8, samples_u8) -> None"},
    {"one_step_summary_finish", msl_one_step_summary_finish, METH_VARARGS,
     "one_step_summary_finish(summary) -> dict"},
    {"one_step_summary", msl_one_step_summary, METH_VARARGS,
     "one_step_summary(out_compare_u8, samples_u8, num_players, profile_rl1) -> dict"},
    {"one_step_eval_samples", msl_one_step_eval_samples, METH_VARARGS,
     "one_step_eval_samples(handle, samples_u8, num_players, profile_rl1) -> dict"},
    {"one_step_eval_buffers", msl_one_step_eval_buffers, METH_VARARGS,
     "one_step_eval_buffers(handle, seed_u8, prev_input_u8, input_u8, ref_u8, num_players, "
     "profile_rl1) -> dict"},
    {"standard_rollout_scan", msl_standard_rollout_scan, METH_VARARGS,
     "standard_rollout_scan(samples_u8, players_u8, num_players, max_records, ucf_enabled, "
     "ucf_cardinals_enabled, profile_rl1, float_fields=(), float_top=0, float_threshold=0.0) -> "
     "dict"},
    {"standard_rollout_scan_buffers", msl_standard_rollout_scan_buffers, METH_VARARGS,
     "standard_rollout_scan_buffers(seed_u8, prev_input_u8, input_u8, ref_u8, players_u8, "
     "num_players, max_records, ucf_enabled, ucf_cardinals_enabled, profile_rl1, float_fields=(), "
     "float_top=0, float_threshold=0.0) -> dict"},
    {"write_gamestate", msl_write_gamestate, METH_VARARGS,
     "write_gamestate(handle, viewpoint_players[batch], out_bytes)"},
    {"write_terminal", msl_write_terminal, METH_VARARGS,
     "write_terminal(handle, out_bytes, max_frame_id=-1)"},
    {"debug_write_processed_input", msl_debug_write_processed_input, METH_VARARGS,
     "debug_write_processed_input(handle, out_bytes)"},
    {"debug_write_stage_state", msl_debug_write_stage_state, METH_VARARGS,
     "debug_write_stage_state(handle, out_bytes)"},
    {"debug_stage_moving_floor_surface", msl_debug_stage_moving_floor_surface_py, METH_VARARGS,
     "debug_stage_moving_floor_surface(handle, batch_index, segment_i) -> transformed floor "
     "surface packet or None"},
    {"debug_write_internals", msl_debug_write_internals, METH_VARARGS,
     "debug_write_internals(handle, out_bytes)"},
    {"debug_write_collision_contacts", msl_debug_write_collision_contacts, METH_VARARGS,
     "debug_write_collision_contacts(handle, out_bytes)"},
    {"debug_write_colldata_ecb", msl_debug_write_colldata_ecb, METH_VARARGS,
     "debug_write_colldata_ecb(handle, out_bytes)"},
    {"debug_force_anim_timebase_enter", msl_debug_force_anim_timebase_enter, METH_VARARGS,
     "debug_force_anim_timebase_enter(handle, batch_index, player_index, anim_start, anim_speed)"},
    {"debug_set_rollout_clock_mode", msl_debug_set_rollout_clock_mode_py, METH_VARARGS,
     "debug_set_rollout_clock_mode(handle, batch_index, mode)"},
    {"debug_get_rollout_clock_mode", msl_debug_get_rollout_clock_mode_py, METH_VARARGS,
     "debug_get_rollout_clock_mode(handle, batch_index) -> int"},
    {"debug_set_camera_mode", msl_debug_set_camera_mode_py, METH_VARARGS,
     "debug_set_camera_mode(handle, batch_index, mode)"},
    {"debug_timebase", msl_debug_timebase_py, METH_VARARGS,
     "debug_timebase(handle, batch_index) -> np.ndarray[float32] shape=(MSL_MAX_PLAYERS,8)"},
    {"debug_hitbox_event_timing", msl_debug_hitbox_event_timing_py, METH_VARARGS,
     "debug_hitbox_event_timing(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitboxEventTiming)]"},
    {"debug_hitbox_sweep_proxy", msl_debug_hitbox_sweep_proxy_py, METH_VARARGS,
     "debug_hitbox_sweep_proxy(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitboxSweepProxy)]"},
    {"debug_hurtcap_slot_flags", msl_debug_hurtcap_slot_flags_py, METH_VARARGS,
     "debug_hurtcap_slot_flags(handle, batch_index, player_index, cap_id) -> "
     "bytes[1,sizeof(MslDebugHurtcapSlotFlags)]"},
    {"debug_hurtcap_geometry_valid", msl_debug_hurtcap_geometry_valid_py, METH_VARARGS,
     "debug_hurtcap_geometry_valid(handle, batch_index, player_index) -> 0/1"},
    {"debug_hurtcap_matrix_valid", msl_debug_hurtcap_matrix_valid_py, METH_VARARGS,
     "debug_hurtcap_matrix_valid(handle, batch_index, player_index, cap_id) -> 0/1"},
    {"debug_poison_hurtcap_matrix", msl_debug_poison_hurtcap_matrix_py, METH_VARARGS,
     "debug_poison_hurtcap_matrix(handle, batch_index, player_index, cap_id)"},
    {"debug_dynamic_pose_state", msl_debug_dynamic_pose_state_py, METH_VARARGS,
     "debug_dynamic_pose_state(handle, batch_index, player_index) -> "
     "bytes[1,sizeof(MslDebugDynamicPoseState)]"},
    {"debug_common_fall_blend_state", msl_debug_common_fall_blend_state_py, METH_VARARGS,
     "debug_common_fall_blend_state(handle, batch_index, player_index) -> (x4, msid)"},
    {"debug_get_fighter_8006cda4_pre_gate_consume_count",
     msl_debug_get_fighter_8006cda4_pre_gate_consume_count_py, METH_VARARGS,
     "debug_get_fighter_8006cda4_pre_gate_consume_count(handle, batch_index, player_index) -> int"},
    {"debug_get_sheik_needle_count", msl_debug_get_sheik_needle_count_py, METH_VARARGS,
     "debug_get_sheik_needle_count(handle, batch_index, player_index) -> int"},
    {"debug_attackairb_continuation_overlap", msl_debug_attackairb_continuation_overlap_py,
     METH_VARARGS,
     "debug_attackairb_continuation_overlap(handle, batch_index, attacker, hb_id, defender, "
     "cap_id) -> float"},
    {"debug_body_matrix_overlap", msl_debug_body_matrix_overlap_py, METH_VARARGS,
     "debug_body_matrix_overlap(handle, batch_index, attacker, hb_id, defender, cap_id) -> float"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
    {"clear_data_dir", msl_clear_data_dir_py, METH_NOARGS,
     "clear_data_dir() -> None. Drop the set_data_dir override; resolution falls back to "
     "the MSL_DATA_DIR env var / 'data'."},
    {"set_data_dir", msl_set_data_dir_py, METH_VARARGS,
     "set_data_dir(path) -> None. Process-wide data root override for the table loaders "
     "(replaces MSL_DATA_DIR env mutation; must be called before init())."},
    {"data_schema_versions", msl_data_schema_versions, METH_NOARGS,
     "data_schema_versions() -> dict of extracted data schema versions expected by this runtime"},
    {"alloc_reset", msl_alloc_reset, METH_NOARGS,
     "Reset C allocation counters (debug/perf guardrail)."},
    {"alloc_stats", msl_alloc_stats, METH_NOARGS,
     "Get C allocation counters (debug/perf guardrail)."},
    {"char_params_ecb_joints", msl_char_params_ecb_joints_py, METH_VARARGS,
     "char_params_ecb_joints(char_id) -> list[int] loaded from data/characters/<char>.json."},
    {"char_params_part_anchors", msl_char_params_part_anchors_py, METH_VARARGS,
     "char_params_part_anchors(char_id) -> dict of runtime part anchors from character data."},
    {"item_article_params", msl_item_article_params_py, METH_VARARGS,
     "item_article_params(char_id) -> dict loaded from MSLITAR1."},
    {"sheik_chain_debug", msl_sheik_chain_debug_py, METH_VARARGS,
     "[DEBUG/TEST-ONLY, not stable API] sheik_chain_debug(handle, batch_index=0, player_index=0) "
     "-> "
     "{links, hitboxes} solved Chain geometry for tests/inspection, or None."},
    {"stage_floor_segment", msl_stage_floor_segment_py, METH_VARARGS,
     "stage_floor_segment(stage_id, segment_i) -> dict from runtime stage collision tables."},
    {"stage_topology_flags", msl_stage_topology_flags_py, METH_VARARGS,
     "stage_topology_flags(stage_id) -> generated stage topology predicate dict."},
    {"stage_fighter_floor_segment", msl_stage_fighter_floor_segment_py, METH_VARARGS,
     "stage_fighter_floor_segment(stage_id, segment_i) -> dict from fighter-solid floor tables."},
    {"stage_ceiling_segment", msl_stage_ceiling_segment_py, METH_VARARGS,
     "stage_ceiling_segment(stage_id, segment_i) -> dict from runtime ceiling tables."},
    {"stage_left_wall_segment", msl_stage_left_wall_segment_py, METH_VARARGS,
     "stage_left_wall_segment(stage_id, segment_i) -> dict from runtime left-wall tables."},
    {"stage_right_wall_segment", msl_stage_right_wall_segment_py, METH_VARARGS,
     "stage_right_wall_segment(stage_id, segment_i) -> dict from runtime right-wall tables."},
    {"stage_raw_line_non_kind", msl_stage_raw_line_non_kind_py, METH_VARARGS,
     "stage_raw_line_non_kind(stage_id, segment_i, skip_kind, forward) -> raw MapLine neighbor."},
    {"stage_static_query", msl_stage_static_query_py, METH_VARARGS,
     "stage_static_query(stage_id, checks, x0, y0, x1, y1, line_skip, joint_skip, joint_only) -> "
     "static mpLib-style line hit dict or None."},
    {"stage_item_line_hit", msl_stage_item_line_hit_py, METH_VARARGS,
     "stage_item_line_hit(stage_id, x0, y0, x1, y1) -> item stage-line hit point or None."},
    {"mpcoll_check_bounding_aabb", msl_mpcoll_check_bounding_aabb_py, METH_VARARGS,
     "mpcoll_check_bounding_aabb(prev_x, prev_y, cur_x, cur_y, prev_l, prev_r, prev_b, prev_t, "
     "cur_l, cur_r, cur_b, cur_t, flags, ledge_snap_x, ledge_snap_y, ledge_snap_h) -> dict"},
    {"mpcoll_end_publication", msl_mpcoll_end_publication_py, METH_VARARGS,
     "mpcoll_end_publication(floor_id, ceiling_id, env_flags, force_floor, floor_arg2, cur_y, "
     "last_y) -> static mpCollEnd finalizer event dict"},
    {"stage_match_flow_roles", msl_stage_match_flow_roles_py, METH_VARARGS,
     "stage_match_flow_roles(stage_id) -> dict of runtime MSLSTG01 match-flow role data."},
    {"hitlist_ring_demo", msl_hitlist_ring_demo_py, METH_VARARGS,
     "hitlist_ring_demo(inserts) -> (ring, ids_u32[12]) (test-only)"},
    {"hitlist_insert_cd_demo", msl_hitlist_insert_cd_demo_py, METH_VARARGS,
     "hitlist_insert_cd_demo(type, rehit_frames) -> inserted victims_1 cooldown (test-only)"},
    {"debug_reset_pose_and_hitboxes_tables", msl_debug_reset_pose_and_hitboxes_tables_py,
     METH_NOARGS, "Reset pose+hitbox global tables (test-only)."},
    {"pose_points_world", msl_pose_points_world_py, METH_VARARGS,
     "pose_points_world(matrices[:,12], local_xyz[:,3], count, model_scale, facing_dir, px, py, "
     "pz, out_xyz[:,3]) -> None"},
    {"derive_camera_target_world", msl_derive_camera_target_world_py, METH_VARARGS,
     "derive_camera_target_world(char_id, animation_index, anim_frame, scale_y, facing, pos_x, "
     "pos_y, pos_z) -> (x,y,z,radius)"},
    {"derive_hitbox_prev_centers", msl_derive_hitbox_prev_centers_py, METH_VARARGS,
     "derive_hitbox_prev_centers(num_players, char_id, action_id, animation_index, action_frame, "
     "anim_frame, pos_x, pos_y, pos_z_or_None, facing, scale_y, rotate_model_or_None, "
     "rotate_valid_or_None) -> (valid,x,y,z)"},
    {"derive_combo_push_timer_seed", msl_derive_combo_push_timer_seed_py, METH_VARARGS,
     "derive_combo_push_timer_seed(combo_count, last_attack_landed, combo_victim_port=None) -> "
     "uint16[:,4]"},
    {"derive_combo_seed_fields", msl_derive_combo_seed_fields_py, METH_VARARGS,
     "derive_combo_seed_fields(num_players, src_ports, hitlag, state_flags, instance_id, "
     "last_hit_by, instance_hit_by=None) -> (victim_port, victim_iid, timer)"},
    {"derive_instance_id_x2073", msl_derive_instance_id_x2073_py, METH_VARARGS,
     "derive_instance_id_x2073(char_id, action_id, action_frame) -> uint8[:]"},
    {"derive_instance_id_counter", msl_derive_instance_id_counter_py, METH_VARARGS,
     "derive_instance_id_counter(fighter_instance_id, item_instance_id) -> uint16[:]"},
    {"derive_item_spawn_id_counter", msl_derive_item_spawn_id_counter_py, METH_VARARGS,
     "derive_item_spawn_id_counter(item_exists, item_spawn_id) -> uint32[:]"},
    {"fill_items_fixed", msl_fill_items_fixed_py, METH_VARARGS,
     "fill_items_fixed(flat Arrow item fields, owner_map, out_items) -> None"},
    {"validation_copy_item_rows_with_illusion", msl_validation_copy_item_rows_with_illusion_py,
     METH_VARARGS,
     "validation_copy_item_rows_with_illusion(seed_u8, ref_u8, items_u8, replay fields, "
     "illusion LUT, players) -> None"},
    {"validation_derive_throw_laser_item_hitlist_buffers",
     msl_validation_derive_throw_laser_item_hitlist_buffers_py, METH_VARARGS,
     "validation_derive_throw_laser_item_hitlist_buffers(seed_u8, hitbox_mask_lut, players) -> "
     "None"},
    {"validation_init_static_buffers", msl_validation_init_static_buffers_py, METH_VARARGS,
     "validation_init_static_buffers(seed_u8, ref_u8, frame_ids, frame_rng, stage_id, "
     "num_players, is_teams, damage_ratio) -> None"},
    {"validation_fill_static_player", msl_validation_fill_static_player_py, METH_VARARGS,
     "validation_fill_static_player(seed_u8, ref_u8, slot, team, handicap, attack_ratio, "
     "defense_ratio, scale_y) -> None"},
    {"validation_fill_visible_player", msl_validation_fill_visible_player_py, METH_VARARGS,
     "validation_fill_visible_player(seed_u8, prev_input_u8, input_u8, ref_u8, slot, stage_id, "
     "source_port0, dmg flags, replay columns...) -> None"},
    {"validation_project_post_cache", msl_validation_project_post_cache_py, METH_VARARGS,
     "validation_project_post_cache(seed_u8, prev_input_u8, input_u8, ref_u8, post/input arrays, "
     "num_players) -> None"},
    {"validation_derive_staling_buffers", msl_validation_derive_staling_buffers_py, METH_VARARGS,
     "validation_derive_staling_buffers(seed_u8, ref_u8, items_u8, anim_frame_f32, "
     "prev_spawn_kinds, num_players) -> staling arrays"},
    {"validation_finalized_frame_indices", msl_validation_finalized_frame_indices_py, METH_VARARGS,
     "validation_finalized_frame_indices(frame_ids_i32) -> int32 keep indices"},
    {"validation_derive_guard_input_prefix", msl_validation_derive_guard_input_prefix_py,
     METH_VARARGS,
     "validation_derive_guard_input_prefix(seed_u8, slot, input/post arrays, params) -> arrays"},
    {"validation_derive_input_history_suffix", msl_validation_derive_input_history_suffix_py,
     METH_VARARGS,
     "validation_derive_input_history_suffix(seed_u8, slot, derived arrays, params) -> arrays"},
    {"validation_derive_fighter_8006cda4_buffers",
     msl_validation_derive_fighter_8006cda4_buffers_py, METH_VARARGS,
     "validation_derive_fighter_8006cda4_buffers(seed_u8, ref_u8, roll_prob, players, "
     "allow_grounded_kneebend) -> None"},
    {"derive_staling_history", msl_derive_staling_history_py, METH_VARARGS,
     "derive_staling_history(src_ports, char_id, action_id, action_frame, animation_index, "
     "percent, stocks, instance_id, last_hit_by, last_hit_by_instance[, item_exists, item_owner, "
     "item_instance_id, item_attack_id, item_attack_instance]) -> "
     "(attack_id, attack_instance, stale_queue_index, stale_move_id, stale_attack_instance)"},
    {"process_stick_i8_units", msl_process_stick_i8_units_py, METH_VARARGS,
     "process_stick_i8_units(raw_x, raw_y, ucf_enabled, cardinals_enabled, deadzone_x, "
     "deadzone_y) -> (proc_x_i8, proc_y_i8, unit_x_f32, unit_y_f32)"},
    {"derive_ucf_pad_buffer_state", msl_derive_ucf_pad_buffer_state_py, METH_VARARGS,
     "derive_ucf_pad_buffer_state(raw_x, raw_y, stick_y_hold_time, ucf_enabled, "
     "cardinals_enabled, deadzone_x, deadzone_y) -> (index, sdrop_up, stick_x[:,4], stick_y[:,4])"},
    {"compute_tilt_timer_axis", msl_compute_tilt_timer_axis_py, METH_VARARGS,
     "compute_tilt_timer_axis(axis_unit, tilt_thresh, start_timer) -> uint8[:]"},
    {"compute_tilt_timer_axis_pre_post", msl_compute_tilt_timer_axis_pre_post_py, METH_VARARGS,
     "compute_tilt_timer_axis_pre_post(axis_unit, tilt_thresh, override_or_None, "
     "override_value, reset_or_None, start_timer_post) -> (pre, post)"},
    {"compute_tilt_timer_y_pre_post_with_fall_fast",
     msl_compute_tilt_timer_y_pre_post_with_fall_fast_py, METH_VARARGS,
     "compute_tilt_timer_y_pre_post_with_fall_fast(stick_y, tilt_thresh, jump_entry, "
     "pre_input_jump_entry_or_None, fastfall_ok, speed_y, on_ground, fastfall_stick_threshold, "
     "fastfall_tilt_max_frames, reset_or_None, start_timer_post) -> (pre, post, fall_fast)"},
    {"derive_damage_hitlag_sdi_reset_post_mask", msl_derive_damage_hitlag_sdi_reset_post_mask_py,
     METH_VARARGS,
     "derive_damage_hitlag_sdi_reset_post_mask(action, hitlag, flags, pos_x, pos_y, stick_x, "
     "stick_y, damage_actions, sdi_step_mul) -> bool[:]"},
    {"derive_damage_time_since_hit_x18ac", msl_derive_damage_time_since_hit_x18ac_py, METH_VARARGS,
     "derive_damage_time_since_hit_x18ac(action, hitlag, hitstun, state_flags) -> int16[:]"},
    {"derive_damage_entry_tilt_timer_reset_post_mask",
     msl_derive_damage_entry_tilt_timer_reset_post_mask_py, METH_VARARGS,
     "derive_damage_entry_tilt_timer_reset_post_mask(action, frame, hitlag, percent, "
     "instance_hit_by, damage_actions) -> bool[:]"},
    {"derive_guard_reflect_timer_plus1", msl_derive_guard_reflect_timer_plus1_py, METH_VARARGS,
     "derive_guard_reflect_timer_plus1(action, hitlag, act_guard_reflect, init_frames) -> "
     "uint8[:]"},
    {"derive_guard_reflect_origin_guardon", msl_derive_guard_reflect_origin_guardon_py,
     METH_VARARGS,
     "derive_guard_reflect_origin_guardon(action, act_guard_reflect, act_guard_on, act_guard) -> "
     "uint8[:]"},
    {"derive_grab_mash_stick_sign_post", msl_derive_grab_mash_stick_sign_post_py, METH_VARARGS,
     "derive_grab_mash_stick_sign_post(stick_x, stick_y, action_id, action_frame, grab_owner, "
     "threshold) -> (x_sign, y_sign)"},
    {"derive_guard_release_lockout_and_lightshield",
     msl_derive_guard_release_lockout_and_lightshield_py, METH_VARARGS,
     "derive_guard_release_lockout_and_lightshield(action_id, shield_hp, hitlag, buttons_held, "
     "trigger_unit, button_mask_lr, button_mask_z, trigger_deadzone, guard_x10_init_frames, "
     "act_guard_on, act_guard, act_guard_reflect, act_guard_set_off) -> (xc, x10, light)"},
    {"derive_guard_special_enable_timer_x1c", msl_derive_guard_special_enable_timer_x1c_py,
     METH_VARARGS,
     "derive_guard_special_enable_timer_x1c(action, hitlag, flags, init, guard_on, guard, "
     "guard_off, guard_reflect, guard_set_off) -> uint8[:]"},
    {"derive_guard_setoff_hitlag_damage_min", msl_derive_guard_setoff_hitlag_damage_min_py,
     METH_VARARGS,
     "derive_guard_setoff_hitlag_damage_min(action, frame, hitlag, mul, base, guard_set_off) -> "
     "uint8[:]"},
    {"derive_guard_setoff_hitlag_exit_phase", msl_derive_guard_setoff_hitlag_exit_phase_py,
     METH_VARARGS,
     "derive_guard_setoff_hitlag_exit_phase(action, hitlag, guard_set_off) -> uint8[:]"},
    {"derive_guard_setoff_post_hitlag_owner", msl_derive_guard_setoff_post_hitlag_owner_py,
     METH_VARARGS,
     "derive_guard_setoff_post_hitlag_owner(action, phase, flags_221c, guard_set_off) -> uint8[:]"},
    {"derive_guard_setoff_exit_frame_speed_seed_lane",
     msl_derive_guard_setoff_exit_frame_speed_seed_lane_py, METH_VARARGS,
     "derive_guard_setoff_exit_frame_speed_seed_lane(action, hitlag, frame_speed, num_players, "
     "guard_set_off) -> float32[:, :]"},
    {"derive_run_x0", msl_derive_run_x0_py, METH_VARARGS,
     "derive_run_x0(action, hitlag, init, run, run_direct, turn_run) -> uint8[:]"},
    {"derive_runbrake_cmd0", msl_derive_runbrake_cmd0_py, METH_VARARGS,
     "derive_runbrake_cmd0(action, anim_frame, char_id, on_by_char, off_by_char, run_brake) -> "
     "uint8[:]"},
    {"derive_dash_x4", msl_derive_dash_x4_py, METH_VARARGS,
     "derive_dash_x4(action, action_frame, dash, turn) -> uint8[:]"},
    {"derive_ecb_lock_timer", msl_derive_ecb_lock_timer_py, METH_VARARGS,
     "derive_ecb_lock_timer(on_ground, action, lock_frames, jump_f, jump_b, aerial_f, aerial_b) -> "
     "uint8[:]"},
    {"derive_ecb_lock_bottom_rel_y", msl_derive_ecb_lock_bottom_rel_y_py, METH_VARARGS,
     "derive_ecb_lock_bottom_rel_y(char, action, anim, anim_frame, on_ground, lock_timer) -> "
     "(float32[:], uint8[:])"},
    {"derive_damage_hitlag_colldata_ecb", msl_derive_damage_hitlag_colldata_ecb_py, METH_VARARGS,
     "derive_damage_hitlag_colldata_ecb(char, action, anim, anim_frame, rate, facing, ground, "
     "hitlag) -> (bottom, top, left, right, side, valid)"},
    {"derive_turn_internals", msl_derive_turn_internals_py, METH_VARARGS,
     "derive_turn_internals(action, frame, facing, stick_x, tilt_x, dash_abs, dash_max, "
     "turn_frames, turn, turn_run) -> (frames, has_turned, x8)"},
    {"compute_press_timer_u8", msl_compute_press_timer_u8_py, METH_VARARGS,
     "compute_press_timer_u8(buttons_pressed, press_mask, start_timer) -> uint8[:]"},
    {"compute_lr_press_timer_x67f", msl_compute_lr_press_timer_x67f_py, METH_VARARGS,
     "compute_lr_press_timer_x67f(buttons, trigger, hitlag_or_None, deadzone, lr_mask, z_mask, "
     "start_timer) -> uint8[:]"},
    {"compute_x672_trigger_timer_pre_post", msl_compute_x672_trigger_timer_pre_post_py,
     METH_VARARGS,
     "compute_x672_trigger_timer_pre_post(trigger, prev_or_None, min, guard_or_None, start) -> "
     "(pre, post)"},
    {"derive_downwait_timer", msl_derive_downwait_timer_py, METH_VARARGS,
     "derive_downwait_timer(action, hitstun_or_None, frames, down_damage_u, down_damage_d, "
     "down_wait_u, down_wait_d) -> int16[:]"},
    {"derive_damage_jump_buffer_x14", msl_derive_damage_jump_buffer_x14_py, METH_VARARGS,
     "derive_damage_jump_buffer_x14(action, hitstun, buttons_pressed, stick_y, tilt_y, "
     "hitlag_or_None, tap_threshold, tilt_max, xy_mask, damage_actions) -> uint16[:]"},
    {"derive_damage_meteor_cancel_x1a", msl_derive_damage_meteor_cancel_x1a_py, METH_VARARGS,
     "derive_damage_meteor_cancel_x1a(action, hitstun, source_angle, angle_min, angle_max, "
     "damage_actions) -> uint8[:]"},
    {"derive_damage_post_hitlag_cb_kind", msl_derive_damage_post_hitlag_cb_kind_py, METH_VARARGS,
     "derive_damage_post_hitlag_cb_kind(action, hitstun, damage_actions) -> uint8[:]"},
    {"derive_guard_tilt_state", msl_derive_guard_tilt_state_py, METH_VARARGS,
     "derive_guard_tilt_state(stick_x, stick_y, facing, action, frame, neutral, frame_max, lerp, "
     "guard_on, guard, guard_reflect) -> (x8, x4)"},
    {"derive_shine_release_state", msl_derive_shine_release_state_py, METH_VARARGS,
     "derive_shine_release_state(action, frame, held, hitlag, lag_init, b_mask, shine actions...) "
     "-> "
     "(release_lag, is_release)"},
    {"derive_kneebend_internals", msl_derive_kneebend_internals_py, METH_VARARGS,
     "derive_kneebend_internals(action, buttons, pressed, stick_y, cstick_y, tilt_y, thresholds, "
     "actions...) -> (jump_input, is_short_hop)"},
    {"derive_smash_charge_seed_lanes", msl_derive_smash_charge_seed_lanes_py, METH_VARARGS,
     "derive_smash_charge_seed_lanes(char, action, anim, frame_speed, ground, hitlag, hitstun, "
     "buttons, a_mask) -> (state,frames,hold,saved_rate_q16)"},
    {"derive_magnify_damage_counter_x1910", msl_derive_magnify_damage_counter_x1910_py,
     METH_VARARGS,
     "derive_magnify_damage_counter_x1910(action, flags, inside, percent, optional contact lanes, "
     "interval, limit, amount) -> uint16[:]"},
    {"derive_colanim_internals", msl_derive_colanim_internals_py, METH_VARARGS,
     "derive_colanim_internals(action, frame, hitlag, hitstun, hurtbox, timers, action sets) -> "
     "(x198c, x1990, x1994, x2221_b0, rebirth_fall_x1994)"},
    {"derive_capture_mash_buttons_pressed", msl_derive_capture_mash_buttons_pressed_py,
     METH_VARARGS,
     "derive_capture_mash_buttons_pressed(buttons, l, r, deadzone, a_mask, z_mask, lr_mask) -> "
     "uint16[:, :]"},
    {"derive_capture_grab_hidden_post", msl_derive_capture_grab_hidden_post_py, METH_VARARGS,
     "derive_capture_grab_hidden_post(action, frame, owner, percent, buttons, sticks, frame_speed, "
     "mash signs, constants...) -> hidden capture lanes"},
    {"derive_ledge_cooldown", msl_derive_ledge_cooldown_py, METH_VARARGS,
     "derive_ledge_cooldown(action, hitlag, cooldown_frames) -> uint8[:]"},
    {"derive_cliff_ledge_floor_segment_id", msl_derive_cliff_ledge_floor_segment_id_py,
     METH_VARARGS,
     "derive_cliff_ledge_floor_segment_id(action, facing, on_ground, cooldown, left_floor, "
     "right_floor) -> uint16[:]"},
    {"derive_cliff_option_stick_latch_x8", msl_derive_cliff_option_stick_latch_x8_py, METH_VARARGS,
     "derive_cliff_option_stick_latch_x8(action, main_x, main_y, c_x, c_y, deadzones, "
     "threshold) -> uint8[:]"},
    {"derive_match_flow_timer", msl_derive_match_flow_timer_py, METH_VARARGS,
     "derive_match_flow_timer(action, port0, common timers...) -> uint8[:]"},
    {"derive_match_flow_respawn_slot_cooldown", msl_derive_match_flow_respawn_slot_cooldown_py,
     METH_VARARGS,
     "derive_match_flow_respawn_slot_cooldown(action[:, players], shared_platform) -> uint8[:,6]"},
    {"derive_passivewall_timer", msl_derive_passivewall_timer_py, METH_VARARGS,
     "derive_passivewall_timer(action, frame, total_frames) -> uint8[:]"},
    {"derive_walljump_phase_seed_lanes", msl_derive_walljump_phase_seed_lanes_py, METH_VARARGS,
     "derive_walljump_phase_seed_lanes(action, frame, setup_x_delta, pos_x, pos_y, raw_main_x) -> "
     "(timer, side)"},
    {"derive_entry_end_fall_lock", msl_derive_entry_end_fall_lock_py, METH_VARARGS,
     "derive_entry_end_fall_lock(action, on_ground, entry_end, fall) -> uint8[:]"},
    {"derive_jab_rapid_count", msl_derive_jab_rapid_count_py, METH_VARARGS,
     "derive_jab_rapid_count(action, buttons_released, buttons_pressed, a_mask) -> uint8[:]"},
    {"derive_walk_anim_source_vel", msl_derive_walk_anim_source_vel_py, METH_VARARGS,
     "derive_walk_anim_source_vel(action, char, facing_dir1, frame_speed, divisor LUTs) -> "
     "float32[:]"},
    {"derive_walk_retarget_tick_source_vel", msl_derive_walk_retarget_tick_source_vel_py,
     METH_VARARGS,
     "derive_walk_retarget_tick_source_vel(action, char, facing, anim, ref_af, velocities, LUTs) "
     "-> float32[:]"},
    {"derive_run_anim_source_vel", msl_derive_run_anim_source_vel_py, METH_VARARGS,
     "derive_run_anim_source_vel(action, char, facing_dir1, frame_speed, scaling LUT) -> "
     "float32[:]"},
    {"derive_facing_dir1_sign", msl_derive_facing_dir1_sign_py, METH_VARARGS,
     "derive_facing_dir1_sign(facing, action) -> int8[:]"},
    {"derive_common_fall_blend_seed", msl_derive_common_fall_blend_seed_py, METH_VARARGS,
     "derive_common_fall_blend_seed(char, action, speed_air_x_self, facing_dir, air_drift_max, "
     "threshold, lerp) -> (valid,x4,msid)"},
    {"derive_sheik_needle_seed_lanes", msl_derive_sheik_needle_seed_lanes_py, METH_VARARGS,
     "derive_sheik_needle_seed_lanes(char, action, action_frame, chain_present, sheik_id) -> "
     "(count,timer)"},
    {"derive_sheik_chain_seed_lanes", msl_derive_sheik_chain_seed_lanes_py, METH_VARARGS,
     "derive_sheik_chain_seed_lanes(char, action, buttons, hitlag, sheik_id, b_mask, "
     "release_min) -> (x0,latch)"},
    {"derive_zelda_twin_state_flags_2218", msl_derive_zelda_twin_state_flags_2218_py, METH_VARARGS,
     "derive_zelda_twin_state_flags_2218(char, state_flags, zelda_id) -> uint8[:]"},
    {"derive_marth_counter_hitlag_floor_active", msl_derive_marth_counter_hitlag_floor_active_py,
     METH_VARARGS,
     "derive_marth_counter_hitlag_floor_active(char, action, state_flags) -> uint8[:]"},
    {"derive_fod_platform_motion_with_ground_contact",
     msl_derive_fod_platform_motion_with_ground_contact_py, METH_VARARGS,
     "derive_fod_platform_motion_with_ground_contact(...) -> FoD platform height/velocity lanes"},
    {"derive_fod_floor_skip_segments", msl_derive_fod_floor_skip_segments_py, METH_VARARGS,
     "derive_fod_floor_skip_segments(...) -> uint16[:,players]"},
    {"derive_sheik_vanish_floor_skip_segments", msl_derive_sheik_vanish_floor_skip_segments_py,
     METH_VARARGS,
     "derive_sheik_vanish_floor_skip_segments(char, action, on_ground, ground_id, vanish_timer, "
     "pos_x, pos_y, platform_ids, platform_x0, platform_y0, platform_x1, platform_y1, sheik_id, "
     "travel_frames, ground_contact_min_frames) -> uint16[:,players]"},
    {"derive_specialhi_rotate_model_seed_lane", msl_derive_specialhi_rotate_model_seed_lane_py,
     METH_VARARGS, "derive_specialhi_rotate_model_seed_lane(...) -> (angle, valid)"},
    {"derive_throw_pulse_seed_lanes", msl_derive_throw_pulse_seed_lanes_py, METH_VARARGS,
     "derive_throw_pulse_seed_lanes(...) -> (consumed,crossed_prev,pending)"},
    {"derive_throw_laser_item_hitlist_seed_lanes",
     msl_derive_throw_laser_item_hitlist_seed_lanes_py, METH_VARARGS,
     "derive_throw_laser_item_hitlist_seed_lanes(...) -> "
     "(victim_port,victim_cd,victim_hitbox_mask,victim_iid)"},
    {"derive_item_attack_fields", msl_derive_item_attack_fields_py, METH_VARARGS,
     "derive_item_attack_fields(item fields, fighter attack fields, players[, "
     "prev_frame_spawn_kinds]) -> "
     "(attack_id,attack_instance)"},
    {"derive_item_reflect_damage_mul", msl_derive_item_reflect_damage_mul_py, METH_VARARGS,
     "derive_item_reflect_damage_mul(item fields, fighter fields, powershield_mul, players) -> "
     "float32[:,slots]"},
    {"derive_item_hidden_callback_seed_lanes", msl_derive_item_hidden_callback_seed_lanes_py,
     METH_VARARGS,
     "derive_item_hidden_callback_seed_lanes(seed/ref item fields, action fields, laser and "
     "shield-bounce LUTs) -> "
     "item hidden callback arrays"},
    {"validation_derive_item_hidden_callback_buffers",
     msl_validation_derive_item_hidden_callback_buffers_py, METH_VARARGS,
     "validation_derive_item_hidden_callback_buffers(seed_u8, ref_u8, laser_lut, shield_lut, "
     "needle_lut, players) -> None"},
    {"validation_derive_item_reflect_damage_mul_buffers",
     msl_validation_derive_item_reflect_damage_mul_buffers_py, METH_VARARGS,
     "validation_derive_item_reflect_damage_mul_buffers(seed_u8, items_u8, replay fields, "
     "reflector LUT, powershield mul, players) -> None"},
    {"validation_derive_zelda_din_buffers", msl_validation_derive_zelda_din_buffers_py,
     METH_VARARGS, "validation_derive_zelda_din_buffers(seed_u8, items_u8, players) -> None"},
    {"validation_derive_yoshi_shyguy_buffers", msl_validation_derive_yoshi_shyguy_buffers_py,
     METH_VARARGS,
     "validation_derive_yoshi_shyguy_buffers(seed_u8, items_u8, frame_rng, params...) -> None"},
    {"derive_yoshi_shyguy_seed_lanes", msl_derive_yoshi_shyguy_seed_lanes_py, METH_VARARGS,
     "derive_yoshi_shyguy_seed_lanes(item fields, params...) -> Shy Guy seed lanes"},
    {"derive_dream_whispy_wind_seed_lanes", msl_derive_dream_whispy_wind_seed_lanes_py,
     METH_VARARGS,
     "derive_dream_whispy_wind_seed_lanes(seed/input/ref bytes, players, stage, speed, eps) -> "
     "(dir,valid,timer)"},
    {"validation_derive_dream_whispy_wind_seed_lanes",
     msl_validation_derive_dream_whispy_wind_seed_lanes_py, METH_VARARGS,
     "validation_derive_dream_whispy_wind_seed_lanes(seed/input/ref bytes, players, stage, speed, "
     "eps) -> (dir,valid,timer)"},
    {"derive_illusion_seed_position_updates", msl_derive_illusion_seed_position_updates_py,
     METH_VARARGS,
     "derive_illusion_seed_position_updates(item fields, fighter fields, illusion LUT) -> "
     "(mask,pos_x,pos_y)"},
    {"trim_stale_hitlist_seed_bridge", msl_trim_stale_hitlist_seed_bridge_py, METH_VARARGS,
     "trim_stale_hitlist_seed_bridge(hitlist arrays, replay fields, lfs allow lane, constants) -> "
     "None"},
    {"derive_attacker_shield_ground_kb_vel", msl_derive_attacker_shield_ground_kb_vel_py,
     METH_VARARGS,
     "derive_attacker_shield_ground_kb_vel(replay fields, LUTs, constants) -> float32[:,4]"},
    {"derive_guardsetoff_frame_speed_overrides", msl_derive_guardsetoff_frame_speed_overrides_py,
     METH_VARARGS,
     "derive_guardsetoff_frame_speed_overrides(replay fields, LUTs, constants) -> float32[:,4]"},
    {"derive_frame_speed_mul_f32", msl_derive_frame_speed_mul_f32_py, METH_VARARGS,
     "derive_frame_speed_mul_f32(timebase replay columns and source tables) -> float32[:]"},
    {"derive_landing_fallspecial_allow_interrupt",
     msl_derive_landing_fallspecial_allow_interrupt_py, METH_VARARGS,
     "derive_landing_fallspecial_allow_interrupt(action_id, char_id, origin table) -> uint8[:]"},
    {"derive_shield_contact_seed_bridge", msl_derive_shield_contact_seed_bridge_py, METH_VARARGS,
     "derive_shield_contact_seed_bridge(hitlist arrays, replay fields, LUTs, constants) -> "
     "(shield_hit_int_damage, shield_damage_taken)"},
    {"derive_rebound_seed_lanes", msl_derive_rebound_seed_lanes_py, METH_VARARGS,
     "derive_rebound_seed_lanes(replay fields, speeds, constants) -> (ground_accel_2, anim_rate)"},
    {"derive_mpcoll_wall_seed_lanes", msl_derive_mpcoll_wall_seed_lanes_py, METH_VARARGS,
     "derive_mpcoll_wall_seed_lanes(action, frame, hitlag, hitstun, pos_x, pos_y, stage, "
     "segment arrays...) -> (kind, wall_id)"},
    {"derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes", msl_derive_source_clear_timer_py,
     METH_VARARGS,
     "derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(...) -> (timer, phase)"},
    {"derive_source_clear_grounded_damage_clear_phase_seed_lane",
     msl_derive_source_clear_grounded_damage_clear_phase_py, METH_VARARGS,
     "derive_source_clear_grounded_damage_clear_phase_seed_lane(...) -> uint8[:]"},
    {"derive_source_clear_terminal_phase_seed_lane", msl_derive_source_clear_terminal_phase_py,
     METH_VARARGS, "derive_source_clear_terminal_phase_seed_lane(...) -> uint8[:]"},
    {"derive_fighter_8006cda4_pre_gate_consume_count",
     msl_derive_fighter_8006cda4_pre_gate_count_py, METH_VARARGS,
     "derive_fighter_8006cda4_pre_gate_consume_count(...) -> uint8[:]"},
    {"derive_source_clear_processhit_damage_pending_phase_seed_lane",
     msl_derive_source_clear_processhit_damage_pending_phase_py, METH_VARARGS,
     "derive_source_clear_processhit_damage_pending_phase_seed_lane(action, flags) -> uint8[:]"},
    {"derive_phantom_damage_pending_seed_lanes", msl_derive_phantom_damage_pending_seed_lanes_py,
     METH_VARARGS,
     "derive_phantom_damage_pending_seed_lanes(percent, hitlag, action, hit_by, iid, players) -> "
     "(damage,timer,source)"},
    {"derive_grounded_overlap_hidden_pos_z", msl_derive_grounded_overlap_hidden_pos_z_py,
     METH_VARARGS,
     "derive_grounded_overlap_hidden_pos_z(num_players, char, action, ground, stocks, pos_x, "
     "pos_z, facing, push_x_lut, push_y_lut, step, z_max) -> float32[:, :]"},
    {"compute_fighter_stick_input_counters", msl_compute_fighter_stick_input_counters_py,
     METH_VARARGS,
     "compute_fighter_stick_input_counters(stick_x, stick_y, tilt_thresh_x, tilt_thresh_y, "
     "start_timer) -> seven uint8 arrays"},
    {"compute_fighter_trigger_input_counters", msl_compute_fighter_trigger_input_counters_py,
     METH_VARARGS,
     "compute_fighter_trigger_input_counters(trigger_unit, trigger_min, start_timer) -> three "
     "uint8 arrays"},
    {"compute_fighter_button_timers", msl_compute_fighter_button_timers_py, METH_VARARGS,
     "compute_fighter_button_timers(buttons_pressed, hitlag_or_None, masks..., start_timer) -> "
     "eight uint8 arrays"},
    {"derive_illusion_ghost_pos012", msl_derive_illusion_ghost_pos012_py, METH_VARARGS,
     "derive_illusion_ghost_pos012(action_id, action_frame, pos_x, pos_y) -> six float32 arrays"},
    {"derive_combat_hitlist_seed_fields", msl_derive_combat_hitlist_seed_fields_py, METH_VARARGS,
     "derive_combat_hitlist_seed_fields(...) -> (cd,iid,hb_valid,hb_cd,hb_iid,shield_kind)"},
    {"ecb_bottom_rel_y", msl_ecb_bottom_rel_y_py, METH_VARARGS,
     "ecb_bottom_rel_y(char_id, animation_index, action_frame) -> float"},
    {"ecb_extents_rel", msl_ecb_extents_rel_py, METH_VARARGS,
     "ecb_extents_rel(char_id, animation_index, action_frame) -> (min_x, max_x, min_y, max_y)"},
    {"anim_pose_matrix", msl_anim_pose_matrix_py, METH_VARARGS,
     "anim_pose_matrix(char_id, msid, frame, part_id) -> np.ndarray[float32] shape=(12,)"},
    {"anim_pose_common_fall_blend_matrix", msl_anim_pose_common_fall_blend_matrix_py, METH_VARARGS,
     "anim_pose_common_fall_blend_matrix(char_id, neutral_msid, target_msid, anim_frame, "
     "part_id, weight) -> np.ndarray[float32] shape=(12,)"},
    {"anim_pose_collision_matrix_f32", msl_anim_pose_collision_matrix_f32_py, METH_VARARGS,
     "anim_pose_collision_matrix_f32(char_id, msid, anim_frame, part_id) -> DEBUG-ONLY "
     "np.ndarray[float32] shape=(12,)"},
    {"move_tables_debug_query", msl_move_tables_debug_query_py, METH_VARARGS,
     "move_tables_debug_query(kind, char_id, action_or_msid, a, b) -> test helper"},
    {"move_tables_throw_has_release", msl_move_tables_throw_has_release_py, METH_VARARGS,
     "move_tables_throw_has_release(char_id, throw_action_id) -> 0/1"},
    {"move_tables_throw_release_frame", msl_move_tables_throw_release_frame_py, METH_VARARGS,
     "move_tables_throw_release_frame(char_id, throw_action_id) -> (ok, release_af)"},
    {"move_tables_throw_release_hit_idx", msl_move_tables_throw_release_hit_idx_py, METH_VARARGS,
     "move_tables_throw_release_hit_idx(char_id, throw_action_id, cur_anim_frame) -> (released, "
     "hit_idx)"},
    {"move_tables_throw_hitbox_params", msl_move_tables_throw_hitbox_params_py, METH_VARARGS,
     "move_tables_throw_hitbox_params(char_id, throw_action_id, hit_idx) -> "
     "(damage, angle, kbg, wsk, bkb, element, sfx_kind, sfx_severity) or None"},
    {"move_tables_throw_release_after_create_hitbox",
     msl_move_tables_throw_release_after_create_hitbox_py, METH_VARARGS,
     "move_tables_throw_release_after_create_hitbox(char_id, throw_action_id) -> 0/1"},
    {"move_tables_throw_cmd1_active", msl_move_tables_throw_cmd1_active_py, METH_VARARGS,
     "move_tables_throw_cmd1_active(char_id, throw_action_id, cur_anim_frame) -> 0/1"},
    {"move_tables_throw_should_spawn_projectile", msl_move_tables_throw_should_spawn_projectile_py,
     METH_VARARGS,
     "move_tables_throw_should_spawn_projectile(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> 0/1"},
    {"move_tables_throw_should_flip_facing", msl_move_tables_throw_should_flip_facing_py,
     METH_VARARGS,
     "move_tables_throw_should_flip_facing(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> 0/1"},
    {"move_tables_throw_crossed_projectile_pulse_frame",
     msl_move_tables_throw_crossed_projectile_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_crossed_projectile_pulse_frame(char_id, throw_action_id, prev_anim_frame, "
     "cur_anim_frame) -> (ok, pulse_frame)"},
    {"move_tables_throw_projectile_first_pulse_frame",
     msl_move_tables_throw_projectile_first_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_projectile_first_pulse_frame(char_id, throw_action_id) -> "
     "(ok, pulse_frame)"},
    {"move_tables_throw_projectile_last_pulse_frame",
     msl_move_tables_throw_projectile_last_pulse_frame_py, METH_VARARGS,
     "move_tables_throw_projectile_last_pulse_frame(char_id, throw_action_id) -> "
     "(ok, pulse_frame)"},
    {"move_tables_throw_projectile_pulse_ordinal",
     msl_move_tables_throw_projectile_pulse_ordinal_py, METH_VARARGS,
     "move_tables_throw_projectile_pulse_ordinal(char_id, throw_action_id, pulse_frame) -> "
     "(ok, ordinal)"},
    {"move_tables_special_pseudo_random_sfx_ranges_crossed",
     msl_move_tables_special_pseudo_random_sfx_ranges_crossed_py, METH_VARARGS,
     "move_tables_special_pseudo_random_sfx_ranges_crossed(char_id, msid, prev_anim_frame, "
     "cur_anim_frame, max_out=8) -> tuple[int, ...]"},
    {"hurtcaps_world", msl_hurtcaps_world_py, METH_VARARGS,
     "hurtcaps_world(handle, batch_index, player_index) -> (caps[MSL_MAX_HURTCAPS,7], count)"},
    {"hitboxes_world", msl_hitboxes_world_py, METH_VARARGS,
     "hitboxes_world(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,10], count)"},
    {"hitboxes_world_full", msl_hitboxes_world_full_py, METH_VARARGS,
     "hitboxes_world_full(handle, batch_index, player_index) -> (hitboxes[MSL_MAX_HITBOXES,16], "
     "count)"},
    {"debug_combat_contacts", msl_debug_combat_contacts_py, METH_VARARGS,
     "debug_combat_contacts(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_hitlist_fighter_contains", msl_debug_hitlist_fighter_contains_py, METH_VARARGS,
     "debug_hitlist_fighter_contains(handle, batch_index, attacker, hb_id, victim) -> 0/1"},
    {"debug_hitlist_fighter_capsule", msl_debug_hitlist_fighter_capsule_py, METH_VARARGS,
     "debug_hitlist_fighter_capsule(handle, batch_index, attacker, hb_id) -> "
     "bytes[1,sizeof(MslDebugHitlistCapsule)]"},
    {"debug_combat_contacts_filtered", msl_debug_combat_contacts_filtered_py, METH_VARARGS,
     "debug_combat_contacts_filtered(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_combat_select_body_hits", msl_debug_combat_select_body_hits_py, METH_VARARGS,
     "debug_combat_select_body_hits(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContact)], count)"},
    {"debug_combat_contacts_classified", msl_debug_combat_contacts_classified_py, METH_VARARGS,
     "debug_combat_contacts_classified(handle, batch_index, max_contacts=256) -> (bytes[max, "
     "sizeof(MslDebugCombatContactClassified)], count)"},
    {"debug_combat_contacts_classified_filtered", msl_debug_combat_contacts_classified_filtered_py,
     METH_VARARGS,
     "debug_combat_contacts_classified_filtered(handle, batch_index, max_contacts=256) -> "
     "(bytes[max, sizeof(MslDebugCombatContactClassified)], count)"},
    {"debug_shield_candidate_decisions", msl_debug_shield_candidate_decisions_py, METH_VARARGS,
     "debug_shield_candidate_decisions(handle, batch_index, max_rows=256) -> "
     "(bytes[max, sizeof(MslDebugShieldCandidateDecision)], count)"},
    {"debug_shield_bubbles_world", msl_debug_shield_bubbles_world_py, METH_VARARGS,
     "debug_shield_bubbles_world(handle, batch_index) -> np.ndarray[float32] "
     "shape=(MSL_MAX_PLAYERS,4)"},
    {"debug_clear_hitboxes_world", msl_debug_clear_hitboxes_world_py, METH_VARARGS,
     "debug_clear_hitboxes_world(handle, batch_index, player_index)"},
    {"debug_set_hitbox_world", msl_debug_set_hitbox_world_py, METH_VARARGS,
     "debug_set_hitbox_world(handle, batch_index, player_index, hitbox_id, x,y,z,radius,damage, "
     "enabled=1)"},
    {"debug_set_hitbox_flags", msl_debug_set_hitbox_flags_py, METH_VARARGS,
     "debug_set_hitbox_flags(handle, batch_index, player_index, hitbox_id, hitbox_flags_u16)"},
    {"debug_set_hitbox_group", msl_debug_set_hitbox_group_py, METH_VARARGS,
     "debug_set_hitbox_group(handle, batch_index, player_index, hitbox_id, hit_group_0_7)"},
    {"debug_set_hitbox_enable_edge", msl_debug_set_hitbox_enable_edge_py, METH_VARARGS,
     "debug_set_hitbox_enable_edge(handle, batch_index, player_index, hitbox_id, enable_edge_u8)"},
    {"debug_set_hitbox_element", msl_debug_set_hitbox_element_py, METH_VARARGS,
     "debug_set_hitbox_element(handle, batch_index, player_index, hitbox_id, element_u8)"},
    {"debug_set_hitbox_kb_params", msl_debug_set_hitbox_kb_params_py, METH_VARARGS,
     "debug_set_hitbox_kb_params(handle, batch_index, player_index, hitbox_id, angle_deg_u16, "
     "kbg_u16, wsk_u16, bkb_u16)"},
    {"debug_clear_hurtcaps_world", msl_debug_clear_hurtcaps_world_py, METH_VARARGS,
     "debug_clear_hurtcaps_world(handle, batch_index, player_index)"},
    {"debug_set_hurtcap_world", msl_debug_set_hurtcap_world_py, METH_VARARGS,
     "debug_set_hurtcap_world(handle, batch_index, player_index, hurtcap_id, "
     "ax,ay,az,bx,by,bz,radius)"},
    {"debug_set_hurtcap_height", msl_debug_set_hurtcap_height_py, METH_VARARGS,
     "debug_set_hurtcap_height(handle, batch_index, player_index, hurtcap_id, height_u8)"},
    {"debug_set_hurtcap_enabled", msl_debug_set_hurtcap_enabled_py, METH_VARARGS,
     "debug_set_hurtcap_enabled(handle, batch_index, player_index, hurtcap_id, enabled=0/1)"},
    {"debug_combat_resolve", msl_debug_combat_resolve_py, METH_VARARGS,
     "debug_combat_resolve(handle) -> run combat_resolve() only"},
    {"debug_run_item_collision_phase", msl_debug_run_item_collision_phase_py, METH_VARARGS,
     "debug_run_item_collision_phase(handle) -> run items_update_collision_phase() only. "
     "DEBUG/TESTING ONLY: mutating phase; use only on a controlled, freshly-reseeded handle (not "
     "as "
     "part of normal stepping -- it double-applies item collision otherwise)."},
    {"debug_set_hitlag", msl_debug_set_hitlag_py, METH_VARARGS,
     "debug_set_hitlag(handle, batch_index, player_index, hitlag_frames_u16)"},
    {"debug_set_sheik_vanish_smoke_accessory_pending",
     msl_debug_set_sheik_vanish_smoke_accessory_pending_py, METH_VARARGS,
     "debug_set_sheik_vanish_smoke_accessory_pending(handle, batch_index, player_index, pending)"},
    {"debug_set_damage_source", msl_debug_set_damage_source_py, METH_VARARGS,
     "debug_set_damage_source(handle, batch_index, player_index, last_hit_by_u8, "
     "instance_hit_by_u16)"},
    {"debug_set_damage_phase", msl_debug_set_damage_phase_py, METH_VARARGS,
     "debug_set_damage_phase(handle, batch_index, player_index, action_id_u16, hitstun_u16, "
     "damage_time_since_hit_i16, on_ground_u8)"},
    {"debug_set_phantom_damage", msl_debug_set_phantom_damage_py, METH_VARARGS,
     "debug_set_phantom_damage(handle, batch_index, player_index, pending_damage_f32, timer_u16, "
     "source_slot_u8)"},
    {"debug_set_prev_action_id", msl_debug_set_prev_action_id_py, METH_VARARGS,
     "debug_set_prev_action_id(handle, batch_index, player_index, prev_action_id_u16)"},
    {"debug_set_grab_owner_port", msl_debug_set_grab_owner_port_py, METH_VARARGS,
     "debug_set_grab_owner_port(handle, batch_index, player_index, grab_owner_port_u8)"},
    {"debug_set_smash_charge_state", msl_debug_set_smash_charge_state_py, METH_VARARGS,
     "debug_set_smash_charge_state(handle, batch_index, player_index, state_u8, frames_u8, "
     "hold_frames_max_u8)"},
    {"debug_set_hit_status_override", msl_debug_set_hit_status_override_py, METH_VARARGS,
     "debug_set_hit_status_override(handle, batch_index, player_index, status_i32)"},
    {"debug_point_segment_dist2", msl_debug_point_segment_dist2_py, METH_VARARGS,
     "debug_point_segment_dist2(px,py,pz, ax,ay,az, bx,by,bz) -> (dist2, t)"},
    {"anim_bake_ssanim01", msl_anim_bake_ssanim01_py, METH_VARARGS,
     "anim_bake_ssanim01(rest_rot, rest_pos, rest_scl, parent_part, part_flags, order, "
     "local_parts, joint_parts, "
     "update_parts, fobj_starts, fobj_desc, ad_source, frame_count, inv_scale_part, "
     "inv_model_scale, end_frame, aobj_loop) -> "
     "(mats_bytes, locals_bytes, transn_bytes)"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "melee_sim._native", NULL, -1, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit__native(void) {
  import_array();
  return PyModule_Create(&moduledef);
}
