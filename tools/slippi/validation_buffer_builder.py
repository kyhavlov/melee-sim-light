from __future__ import annotations

from tools.slippi.validation_buffer_common import (  # noqa: F401
    functools,
    json,
    struct,
    dataclass,
    Path,
    SimpleNamespace,
    Any,
    np,
    pa,
    _read_slippi,
    COMPARE_DTYPE,
    INPUT_DTYPE,
    SEED_DTYPE,
    load_action_state_tables,
    hitstun_u16_from_misc_as_and_state_flags3,
    item_article_kind_set,
    item_article_values_by_sim_char,
    STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT,
    dream_whispy_metadata,
    fountain_of_dreams_default_platform_heights,
    fountain_of_dreams_platform_motion_params,
    read_mslstg01_v7,
    stage_metadata_path_for_stage_id,
    yoshi_shyguy_metadata,
    read_callback_manifest,
    read_mslmso01_v1,
    finalized_frame_indices,
    replay_path_for_peppi,
    read_mslftsc1_v1,
    team_attack_on_from_start,
    MSL_MS_CLASS_ATTACK_AIR,
    FOD_SKIP_ECB_VERTICAL_UNIT,
    FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP,
    FOD_FLOOR_X_END_CLAMP,
    FOD_FLOOR_Y_BIAS,
    FOD_STAGE_LINE_DX_EPSILON,
    _STAGE_KIND_BY_ID,
    _MATCH_FLOW_ACTION_IDS,
    _ascontiguousarray,
    _u8_lut_from_items,
    _u8_lut_from_pairs,
    _path_cache_key,
    _load_json_file_cached,
    _load_bytes_file_cached,
    _read_mslstg01_v7_cached,
    _load_json_file,
    _load_bytes_file,
    _read_mslstg01,
    ValidationReplayBuffers,
    _SampleParts,
    _ManifestPreprocessTables,
    _env_damage_int,
    _load_character_attrs,
    _load_moves_file,
    _load_common_data,
    manifest_registry_chars,
    require_replay_chars_in_manifest,
    _manifest_preprocess_tables,
    _load_u8_character_attr_lut_cached,
    _load_u8_character_attr_lut,
    _multijump_ladder_lut,
    _load_f32_character_attr_lut_cached,
    _load_f32_character_attr_lut,
    _derive_common_fall_blend_seed,
    _derive_sheik_needle_seed_lanes,
    _derive_sheik_chain_seed_lanes,
    _derive_zelda_twin_state_flags_2218,
    PortStatic,
    _to_numpy,
)
from tools.slippi.validation_buffer_stage import (  # noqa: F401
    _load_stage_segments_for_seed_cached,
    _load_stage_segments_for_seed,
    _motion_state_owner_actions_by_char,
    _move_submotion_ids_for_char,
    _attackair_first_hitbox_phase_by_char_action,
    _stage_ledge_floor_ids,
    _fod_platform_heights_from_frames,
    _fod_platform_height_transform_records,
    _derive_fod_floor_skip_segments,
    _derive_sheik_vanish_floor_skip_segments,
    _fod_platform_heights_with_ground_contact,
    _fod_platform_motion_with_ground_contact,
    _fod_hidden_return_timers,
    _fod_visible_choice_lanes,
    _dir_to_facing,
    _airborne_to_on_ground,
    _post_position_z,
    _derive_grounded_overlap_hidden_pos_z,
    _u8_from_float01,
    _int8_from_float_axis,
    _stick_i8_from_unit_stick,
    _u16_from_float_frames,
    _i16_from_state_age,
    _f32_from_state_age,
)
from tools.slippi.validation_buffer_seed import (  # noqa: F401
    _port_name,
    _seed_bridge_owner_matches_attacker,
    _seed_bridge_trim_indefinite_lanes,
    _derive_ledge_cooldown,
    _derive_cliff_ledge_floor_segment_id,
    _derive_cliff_option_stick_latch_x8,
    _derive_match_flow_timer,
    _derive_passivewall_timer,
    _derive_attackdash_x0_seed_lane,
    _derive_walljump_phase_seed_lanes,
    _derive_mpcoll_wall_seed_lanes,
    _derive_entry_end_fall_lock,
    _derive_opening_input_lock_timer,
    _stage_respawn_points_y,
    _respawn_point_y_for_stage_port,
    _load_throw_pulse_seed_tables,
    _load_specialn_loop_cmd0_windows,
    _load_runbrake_cmd0_seed_tables,
    _load_source_clear_terminal_followup_tables,
    _load_action_x9_b1_tables,
    _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes,
    _derive_source_clear_grounded_damage_clear_phase_seed_lane,
    _derive_source_clear_processhit_damage_pending_phase_seed_lane,
    _derive_phantom_damage_pending_seed_lanes,
    _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane,
    _derive_source_clear_terminal_phase_seed_lane,
)
from tools.slippi.validation_buffer_items import (  # noqa: F401
    _derive_throw_pulse_seed_lanes,
    _derive_throw_laser_item_hitlist_seed_lanes,
    _fill_items_fixed,
    _yoshi_shyguy_params,
    _item_common_params,
    _laser_shot_item_kinds,
    _derive_yoshi_shyguy_native_lanes,
    _derive_yoshi_shyguy_prev_vel_y,
    _derive_yoshi_shyguy_dyn_y_phase,
    _derive_yoshi_shyguy_seed_lanes,
    _structured_rows_as_bytes,
    _structured_rows_as_writable_bytes,
    _dream_whispy_params,
    _derive_dream_whispy_wind_seed_lanes,
    _materialize_illusion_seed_positions,
    _derive_illusion_seed_position_updates,
    derive_illusion_ghost_pos012,
    derive_illusion_ghost_pos01,
    _derive_item_attack_fields,
    _derive_item_reflect_damage_mul,
    _damage_hurt_height_from_action,
    _derive_item_hidden_callback_seed_lanes,
)
from tools.slippi.validation_buffer_fighter import (  # noqa: F401
    _ascontiguousarray,
    _to_numpy,
    _derive_landing_fallspecial_allow_interrupt_seed_lane,
    _derive_jab_rapid_count_seed_lane,
    _derive_walk_anim_source_vel_seed_lane,
    _derive_walk_retarget_tick_source_vel_seed_lane,
    _derive_run_anim_source_vel_seed_lane,
    _team_id_from_start_player,
    _derive_match_flow_pending_rebirth_char_id,
    _derive_match_flow_pending_rebirth_state_flags_2218,
    _derive_facing_dir1_sign,
    _derive_specialhi_rotate_model_seed_lane,
    _derive_kb_smashcharge_active_from_post,
    _derive_smash_charge_seed_lanes,
)

def warm_validation_generated_data_cache(*, data_root: Path=Path('data'), stage_ids: tuple[int, ...]=()) -> None:
    """Warm immutable generated-data readers before forked validation workers start."""
    data_root = Path(data_root)
    manifest_chars = manifest_registry_chars(data_root)
    _load_common_data(data_root)
    _item_common_params()
    _dream_whispy_params()
    _yoshi_shyguy_params()
    _load_bytes_file(data_root / 'staling' / 'weights.bin')
    _load_f32_character_attr_lut(data_root, 'air_drift_max')
    _load_u8_character_attr_lut(data_root, 'turn_frames')
    from tools.slippi.anim_timebase import load_end_frame_tables
    load_end_frame_tables(data_root)
    load_action_state_tables(str(data_root))
    _load_action_x9_b1_tables(data_root=data_root)
    _load_specialn_loop_cmd0_windows(data_root=data_root)
    _load_runbrake_cmd0_seed_tables(data_root=data_root)
    _load_source_clear_terminal_followup_tables(data_root=data_root)
    item_article_kind_set(data_root, 'blaster_shot_itkind')
    item_article_kind_set(data_root, 'needle_throw_itkind')
    item_article_kind_set(data_root, 'side_special_illusion_itkind')
    item_article_values_by_sim_char(data_root, 'blaster_shot_itkind')
    _laser_shot_item_kinds(data_root / 'items' / 'lasers.bin')
    for _cid, name in manifest_chars:
        _load_character_attrs(data_root, name)
        _load_moves_file(data_root, name)
        special_msids = data_root / 'special_msids' / f'{name}.json'
        if special_msids.exists():
            _load_json_file(special_msids)
        script_manifest = data_root / 'scripts' / f'{name}_manifest.json'
        if script_manifest.exists():
            _load_json_file(script_manifest)
    for stage_id in sorted({int(s) for s in stage_ids if int(s) > 0}):
        _load_stage_segments_for_seed(stage_id=stage_id, data_root=data_root)
        _stage_respawn_points_y(stage_id=stage_id, data_dir=str(data_root))

def build_validation_buffers_from_slp(*, slp_path: str, ports: list[int] | None=None, ucf_enabled: bool=True, ucf_cardinals_1_0_enabled: bool=False) -> ValidationReplayBuffers:
    """Build native validation buffers from a replay."""

    class Args:
        pass
    a = Args()
    a.slp = slp_path
    a.out = None
    a.ports = None if ports is None else ','.join((str(p) for p in ports))
    a.ucf_enabled = bool(ucf_enabled)
    a.ucf_cardinals_1_0_enabled = bool(ucf_cardinals_1_0_enabled)
    return _build_validation_buffers_impl(a)

def _build_validation_buffers_impl(args) -> ValidationReplayBuffers:
    from tools.slippi.combat_history import derive_combat_hitlist_seed_fields, derive_hitbox_prev_center_seed_fields
    from tools.slippi.damage_history import derive_damage_time_since_hit_x18ac
    from tools.slippi.anim_timebase import derive_frame_speed_mul_f32, load_end_frame_tables
    from tools.slippi.seed_history import derive_instance_id_counter, derive_instance_id_x2073, derive_item_spawn_id_counter, derive_colanim_internals, derive_downwait_timer, derive_damage_jump_buffer_x14, derive_damage_meteor_cancel_x1a, derive_damage_entry_tilt_timer_reset_post_mask, derive_damage_hitlag_sdi_reset_post_mask, derive_damage_post_hitlag_cb_kind, derive_camera_box_visible_x221f_b0, derive_camera_target_point_inside_stage_cam_bounds, derive_camera_target_world, derive_magnify_damage_counter_x1910, derive_rebirth_camera_anchor_y, derive_capture_mash_buttons_pressed, derive_capture_grab_hidden_post, derive_grab_mash_stick_sign_post, derive_grab_owner_port, derive_grab_owner_port_2p, derive_seed_prev_action_post, derive_dash_x4, derive_runbrake_cmd0, derive_shine_release_state, derive_run_x0, derive_ecb_lock_timer, derive_ecb_lock_bottom_rel_y, derive_damage_hitlag_colldata_ecb, load_shield_tilt_table_meta, process_stick_i8_units
    with replay_path_for_peppi(args.slp) as peppi_path:
        game = _read_slippi(str(peppi_path), False)
    frames_all = game.frames
    if frames_all is None or len(frames_all) == 0:
        raise ValueError('Replay has no frames')
    ucf_enabled = bool(getattr(args, 'ucf_enabled', True))
    ucf_cardinals_1_0_enabled = bool(getattr(args, 'ucf_cardinals_1_0_enabled', False))
    if args.ports is not None:
        src_ports = [int(x.strip()) for x in args.ports.split(',') if x.strip()]
        if any((p < 1 or p > 4 for p in src_ports)):
            raise ValueError(f'--ports must be in 1..4, got {src_ports}')
        src_ports = sorted(src_ports)
    else:
        players = list(game.start.get('players', []))
        src_ports = []
        for p in players:
            if str(p.get('type')) != 'Human':
                continue
            port = str(p.get('port'))
            if not port.startswith('P'):
                continue
            src_ports.append(int(port[1:]))
        src_ports = sorted(src_ports)
    if len(src_ports) not in (2, 4):
        raise ValueError(f'Expected 2 or 4 selected ports, got {src_ports}')
    src_port_names = [_port_name(p) for p in src_ports]
    num_players = len(src_port_names)
    static_by_port: dict[int, PortStatic] = {}
    ratios_by_port: dict[int, tuple[float, float, float]] = {}
    dmg_flags_by_port: dict[int, tuple[int, int]] = {}
    for p in game.start.get('players', []):
        port = str(p.get('port', ''))
        if not port.startswith('P'):
            continue
        port_1based = int(port[1:])
        static_by_port[port_1based] = PortStatic(team_id=_team_id_from_start_player(p), char_id=int(p.get('character', 0)), handicap=int(p.get('handicap', 9)))
        ratios_by_port[port_1based] = (float(p.get('offense_ratio', 1.0)), float(p.get('defense_ratio', 1.0)), float(p.get('model_scale', 1.0)))
        raw_xc = int(p.get('bitfield', 0)) & 255
        dmg_x2225_b7 = 1 if raw_xc & 1 else 0
        dmg_x2224_b2 = 0
        dmg_flags_by_port[port_1based] = (dmg_x2225_b7, dmg_x2224_b2)
    frame_ids_all = _to_numpy(frames_all.field('id'))
    keep = finalized_frame_indices(frame_ids_all)
    frames = frames_all.take(pa.array(keep))
    frame_ids = _to_numpy(frames.field('id')).astype(np.int32)
    n_frames = int(len(frames))
    if n_frames < 2:
        raise ValueError('Not enough frames for one-step validation buffers')
    frame_pre_random_seed = _to_numpy(frames.field('start').field('random_seed')).astype(np.uint32)
    n_samples = n_frames - 1
    samples = _SampleParts(n_samples)
    stage_id = int(game.start.get('stage', 0))
    is_teams = int(bool(game.start.get('is_teams', False)))
    team_attack_on = team_attack_on_from_start(game.start)
    if int(num_players) > 2 and is_teams and (team_attack_on is not True):
        raise ValueError(f'{args.slp}: selected 4-player teams replay has Team Attack OFF or unknown; melee-sim-light doubles validation currently supports Team Attack ON only')
    data_root = Path('data')
    common = _load_common_data(data_root)
    lstick_deadzone_x = float(common['lstick_deadzone_x'])
    lstick_deadzone_y = float(common['lstick_deadzone_y'])
    lstick_tilt_x_thresh = float(common['lstick_tilt_x_thresh'])
    lstick_tilt_y_thresh = float(common['lstick_tilt_y_thresh'])
    dash_flick_abs = float(common['dash_flick_abs'])
    dash_flick_tilt_max_frames = int(common['dash_flick_tilt_max_frames'])
    tap_jump_threshold = float(common['tap_jump_threshold'])
    dash_run_jump_stick_y_threshold = float(common['dash_run_jump_stick_y_threshold'])
    platform_air_land_stick_y_threshold = float(common['platform_air_land_stick_y_threshold'])
    floor_skip_frames = int(common['floor_skip_frames'])
    tap_jump_release_threshold = float(common['tap_jump_release_threshold'])
    tap_jump_tilt_max_frames = int(common['tap_jump_tilt_max_frames'])
    grab_mash_stick_threshold = float(common['grab_mash_stick_threshold'])
    fastfall_stick_threshold = float(common['fastfall_stick_threshold'])
    fastfall_tilt_max_frames = int(common['fastfall_tilt_max_frames'])
    guard_stick_lerp_x44c = float(common['guard_stick_lerp_x44c'])
    lcancel_window_frames = int(common['lcancel_window_frames'])
    lcancel_lag_div = float(common['lcancel_lag_div'])
    landing_fall_special_lag_frames = float(common['landing_fall_special_lag_frames'])
    common_fall_blend_threshold = float(common['common_fall_blend_air_drift_threshold'])
    common_fall_blend_lerp = float(common['common_fall_blend_lerp'])
    air_drift_max_by_char = _load_f32_character_attr_lut(data_root, 'air_drift_max')
    laser_item_types = item_article_kind_set(data_root, 'blaster_shot_itkind')
    needle_throw_item_types = item_article_kind_set(data_root, 'needle_throw_itkind')
    shield_bounce_item_types = tuple(sorted(set(laser_item_types) | set(needle_throw_item_types)))
    laser_kind_by_char = item_article_values_by_sim_char(data_root, 'blaster_shot_itkind')
    throw_laser_hitbox_masks = {int(laser_kind_by_char[1]): np.uint8(3), int(laser_kind_by_char[22]): np.uint8(12)}
    illusion_item_kinds = item_article_kind_set(data_root, 'side_special_illusion_itkind')
    end_frames = load_end_frame_tables(data_root)
    stage_segments: list[dict] = []
    stage_segments = _load_stage_segments_for_seed(stage_id=stage_id, data_root=data_root)

    def _derive_marth_counter_hitlag_floor_active(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, state_flags_u8: np.ndarray) -> np.ndarray:
        try:
            import msl_binding
        except ImportError as exc:
            raise RuntimeError('native msl_binding.derive_marth_counter_hitlag_floor_active is required; run `make build`') from exc
        return msl_binding.derive_marth_counter_hitlag_floor_active(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(state_flags_u8, dtype=np.uint8))

    def _derive_sheik_vanish_travel_timer(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, sheik_internal_id: int | None, travel_frames: int) -> np.ndarray:
        char_arr = np.asarray(char_id_u8, dtype=np.uint8)
        action_arr = np.asarray(action_id_u16, dtype=np.uint16)
        if char_arr.ndim == 2:
            out_2d = np.zeros(action_arr.shape, dtype=np.uint8)
            for slot in range(action_arr.shape[1]):
                out_2d[:, slot] = _derive_sheik_vanish_travel_timer(char_id_u8=char_arr[:, slot], action_id_u16=action_arr[:, slot], sheik_internal_id=sheik_internal_id, travel_frames=travel_frames)
            return out_2d
        out = np.zeros(action_arr.shape[0], dtype=np.uint8)
        if sheik_internal_id is None or travel_frames <= 0:
            return out
        travel = (char_arr == np.uint8(sheik_internal_id)) & ((action_arr == np.uint16(356)) | (action_arr == np.uint16(359)))
        idx = np.flatnonzero(travel)
        if idx.size == 0:
            return out
        split_at = np.flatnonzero(np.diff(idx) != 1) + 1
        for seg in np.split(idx, split_at):
            remaining = int(travel_frames) - np.arange(seg.size, dtype=np.int16)
            remaining = np.clip(remaining, 1, 255).astype(np.uint8)
            out[seg] = remaining
        return out

    def _stale_multiplier_from_seed_queue(queue_index: int, queue_move_ids: np.ndarray, move_id: int) -> float:
        if move_id in (65535, 1):
            return 1.0
        qi = int(queue_index) if 0 <= int(queue_index) < 10 else 0
        pos = qi - 1 if qi != 0 else 9
        mult = 1.0
        for i in range(9):
            mid = int(queue_move_ids[pos])
            if mid == 0:
                return mult
            if mid == move_id:
                mult -= float(stale_weights[i])
            pos = pos - 1 if pos != 0 else 9
        return mult
    stale_weights_buf = _load_bytes_file(data_root / 'staling' / 'weights.bin')
    stale_weight_count = int(struct.unpack_from('<H', stale_weights_buf, 12)[0])
    stale_weights = struct.unpack_from('<' + 'f' * stale_weight_count, stale_weights_buf, 20)
    manifest_tables = _manifest_preprocess_tables(_path_cache_key(data_root))
    manifest_chars = manifest_tables.manifest_chars
    char_landing_air_lag_frames = manifest_tables.char_landing_air_lag_frames
    char_fallspecial_origin_lag = manifest_tables.char_fallspecial_origin_lag
    char_fallspecial_origin_allow_interrupt = manifest_tables.char_fallspecial_origin_allow_interrupt
    char_walk_divisors = manifest_tables.char_walk_divisors
    char_walk_max = manifest_tables.char_walk_max
    char_run_scaling = manifest_tables.char_run_scaling
    char_can_walljump = manifest_tables.char_can_walljump
    char_walljump_setup_x_delta_threshold = manifest_tables.char_walljump_setup_x_delta_threshold
    sheik_char_id = manifest_tables.sheik_char_id
    zelda_char_id = manifest_tables.zelda_char_id
    sheik_vanish_travel_frames = manifest_tables.sheik_vanish_travel_frames
    sheik_vanish_ground_contact_min_frames = manifest_tables.sheik_vanish_ground_contact_min_frames
    sheik_chain_release_min_frames = manifest_tables.sheik_chain_release_min_frames
    shield_meta = load_shield_tilt_table_meta()
    neutral_lut = np.zeros(256, dtype=np.uint16)
    frame_max_lut = np.zeros(256, dtype=np.uint16)
    for cid, (neutral, frame_max) in shield_meta.items():
        neutral_lut[np.uint8(cid)] = np.uint16(int(neutral) & 65535)
        frame_max_lut[np.uint8(cid)] = np.uint16(int(frame_max) & 65535)
    act_wait = 14
    act_walk_slow = 15
    act_walk_middle = 16
    act_walk_fast = 17
    act_turn = 18
    act_turn_run = 19
    act_dash = 20
    act_run = 21
    act_run_direct = 22
    act_run_brake = 23
    act_kneebend = 24
    act_jump_f = 25
    act_jump_b = 26
    act_jump_aerial_f = 27
    act_jump_aerial_b = 28
    act_fall = 29
    act_fall_f = 30
    act_fall_b = 31
    act_fall_aerial = 32
    act_fall_aerial_f = 33
    act_fall_aerial_b = 34
    act_fall_special = 35
    act_fall_special_f = 36
    act_fall_special_b = 37
    act_damage_fall = 38
    act_rebirth = 12
    act_rebirth_wait = 13
    act_landing_fall_special = 43
    act_fx_special_n_loop = 342
    act_fx_special_air_n_loop = 345
    act_fx_special_s = 348
    act_fx_special_s_end = 349
    act_fx_special_air_s = 351
    act_fx_special_air_s_end = 352
    act_fx_special_hi = 355
    act_fx_special_air_hi = 356
    act_fx_special_hi_landing = 357
    act_fx_special_hi_fall = 358
    act_fx_special_hi_bound = 359
    act_fx_special_lw_start = 360
    act_fx_special_lw_loop = 361
    act_fx_special_lw_hit = 362
    act_fx_special_lw_end = 363
    act_fx_special_lw_turn = 364
    act_fx_special_air_lw_start = 365
    act_fx_special_air_lw_loop = 366
    act_fx_special_air_lw_hit = 367
    act_fx_special_air_lw_end = 368
    act_fx_special_air_lw_turn = 369
    act_damage_hi_1 = 75
    act_damage_hi_2 = 76
    act_damage_hi_3 = 77
    act_damage_n_1 = 78
    act_damage_n_2 = 79
    act_damage_n_3 = 80
    act_damage_lw_1 = 81
    act_damage_lw_2 = 82
    act_damage_lw_3 = 83
    act_damage_air_1 = 84
    act_damage_air_2 = 85
    act_damage_air_3 = 86
    act_damage_fly_hi = 87
    act_damage_fly_n = 88
    act_damage_fly_lw = 89
    act_damage_fly_top = 90
    act_damage_fly_roll = 91
    act_fly_reflect_wall = 247
    act_fly_reflect_ceil = 248
    act_down_damage_d = 193
    act_attack_11 = 44
    act_attack_12 = 45
    act_attack_13 = 46
    act_attack_dash = 50
    act_attack_lw3 = 57
    act_attack_air_n = 65
    act_attack_air_f = 66
    act_attack_air_b = 67
    act_attack_air_hi = 68
    act_attack_air_lw = 69
    act_attack_lw4 = 64
    act_guard_on = 178
    act_guard = 179
    act_guard_off = 180
    act_guard_set_off = 181
    act_guard_reflect = 182
    act_rebound_stop = 237
    act_rebound = 238
    act_throw_f = 219
    act_throw_b = 220
    act_throw_hi = 221
    act_throw_lw = 222
    act_cliff_catch = 252
    act_cliff_wait = 253
    act_passive_wall = 202
    act_passive_wall_jump = 203
    act_down_bound_u = 183
    act_down_wait_u = 184
    act_down_bound_d = 191
    act_down_wait_d = 192
    act_escape_air = 236
    throw_action_to_move = {int(act_throw_f): 'ftCo_SM_ThrowF', int(act_throw_b): 'ftCo_SM_ThrowB', int(act_throw_hi): 'ftCo_SM_ThrowHi', int(act_throw_lw): 'ftCo_SM_ThrowLw'}
    throw_pulse_frames_by_char_action, throw_cmd1_start_by_char_action, throw_shot_itkind_by_char = _load_throw_pulse_seed_tables(data_root=data_root, throw_action_to_move=throw_action_to_move)
    specialn_loop_cmd0_windows_by_char_msid = _load_specialn_loop_cmd0_windows(data_root=data_root)
    runbrake_cmd0_on_by_char, runbrake_cmd0_off_by_char = _load_runbrake_cmd0_seed_tables(data_root=data_root)
    source_clear_followup_cmd0_on_by_char_action, source_clear_followup_cmd0_off_by_char_action = _load_source_clear_terminal_followup_tables(data_root=data_root)
    action_x9_b1_by_char = _load_action_x9_b1_tables(data_root=data_root)
    source_clear_x18c8_init_frames = 60
    char_fox = 1
    char_falco = 22
    button_mask_xy = 1024 | 2048
    button_mask_lr = 64 | 32
    button_mask_z = 16
    button_mask_a = 256
    button_mask_b = 512
    button_mask_dpad_up = 8
    button_mask_dpad_down = 4
    turn_frames_lut = _load_u8_character_attr_lut(data_root, 'turn_frames')
    multijump_first_lut, multijump_count_lut = _multijump_ladder_lut(data_root)
    reflector_release_lag_lut = np.zeros(256, dtype=np.uint8)
    reflector_release_lag_lut[np.uint8(1)] = np.uint8(_load_character_attrs(data_root, 'fox')['reflector_release_lag_frames'])
    reflector_release_lag_lut[np.uint8(22)] = np.uint8(_load_character_attrs(data_root, 'falco')['reflector_release_lag_frames'])
    reflector_damage_mul_lut = np.ones(256, dtype=np.float32)
    reflector_damage_mul_lut[np.uint8(1)] = np.float32(_load_character_attrs(data_root, 'fox')['reflector_damage_mul'])
    reflector_damage_mul_lut[np.uint8(22)] = np.float32(_load_character_attrs(data_root, 'falco')['reflector_damage_mul'])
    import msl_binding
    msl_binding.validation_init_static_buffers(samples.seed_u8(), samples.ref_u8(), frame_ids, frame_pre_random_seed, int(stage_id), int(num_players), int(is_teams), float(game.start.get('damage_ratio', 1.0)))
    if int(stage_id) == 2:
        fod_defaults = fountain_of_dreams_default_platform_heights(data_root)
        fod_motion_params = fountain_of_dreams_platform_motion_params(data_root)
        fod_height, fod_valid, fod_fresh = _fod_platform_heights_from_frames(frames, n_frames, default_heights=fod_defaults)
        samples['seed_t']['stage_fod_platform_height_f32'] = fod_height[:-1]
        samples['seed_t']['stage_fod_platform_height_valid_u8'] = fod_valid[:-1]
    else:
        fod_fresh = None
        fod_motion_params = None
    items_fixed = _fill_items_fixed(frames, n_frames, src_ports=src_ports)
    for slot, port_1based in enumerate(src_ports):
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0, handicap=9))
        atk, df, scl = ratios_by_port.get(port_1based, (1.0, 1.0, 1.0))
        msl_binding.validation_fill_static_player(samples.seed_u8(), samples.ref_u8(), int(slot), int(st.team_id), int(st.handicap), float(atk), float(df), float(scl))
    post_action_id_u16 = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_age_all = np.zeros((n_frames, 4), dtype=np.int16)
    post_anim_frame_f32_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_char_id_u8 = np.zeros((n_frames, 4), dtype=np.uint8)
    post_stocks_u8_all = np.zeros((n_frames, 4), dtype=np.uint8)
    match_flow_timer_u8_all = np.zeros((n_frames, 4), dtype=np.uint8)
    static_char_id_u8 = np.zeros(4, dtype=np.uint8)
    team_id_u8 = np.zeros(4, dtype=np.uint8)
    for slot, port_1based in enumerate(src_ports):
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0, handicap=9))
        static_char_id_u8[slot] = np.uint8(st.char_id)
        team_id_u8[slot] = np.uint8(st.team_id)
    post_pos_x_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_y_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_z_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_percent_all = np.zeros((n_frames, 4), dtype=np.float32)
    frame_speed_mul_all = np.zeros((n_frames, 4), dtype=np.float32)
    specialhi_rotate_model_all = np.zeros((n_frames, 4), dtype=np.float32)
    specialhi_rotate_model_valid_all = np.zeros((n_frames, 4), dtype=np.uint8)
    post_hitlag_u16_all = np.zeros((n_frames, 4), dtype=np.uint16)
    post_shield_f32_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_animation_index_u32_all = np.zeros((n_frames, 4), dtype=np.uint32)
    lightshield_amount_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_instance_hit_by_u16_all = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_flags_u8 = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    post_turn_has_turned_u8 = np.zeros((n_frames, 4), dtype=np.uint8)
    post_combo_count_u8_all = np.zeros((n_frames, 4), dtype=np.uint8)
    post_last_attack_landed_u8_all = np.zeros((n_frames, 4), dtype=np.uint8)
    post_landing_fallspecial_allow_interrupt = np.zeros((n_frames, 4), dtype=np.uint8)
    ports_struct = frames.field('ports')
    available_ports = set((f.name for f in ports_struct.type))
    for slot, port_name in enumerate(src_port_names):
        if port_name not in available_ports:
            raise ValueError(f'Replay missing port {port_name}; available ports: {sorted(available_ports)}')
        leader = ports_struct.field(port_name).field('leader')
        pre = leader.field('pre')
        post = leader.field('post')
        pre_buttons_physical = _to_numpy(pre.field('buttons_physical')).astype(np.uint16)
        pre_main_x = _to_numpy(pre.field('raw_analog_x')).astype(np.int8)
        pre_main_y = _to_numpy(pre.field('raw_analog_y')).astype(np.int8)
        if pre.type.get_field_index('raw_analog_cstick_x') != -1:
            pre_c_x = _to_numpy(pre.field('raw_analog_cstick_x')).astype(np.int8)
            pre_c_y = _to_numpy(pre.field('raw_analog_cstick_y')).astype(np.int8)
        elif pre.type.get_field_index('cstick') != -1:
            pre_c_x = _stick_i8_from_unit_stick(_to_numpy(pre.field('cstick').field('x')).astype(np.float32))
            pre_c_y = _stick_i8_from_unit_stick(_to_numpy(pre.field('cstick').field('y')).astype(np.float32))
        else:
            pre_c_x = np.zeros(n_frames, dtype=np.int8)
            pre_c_y = np.zeros(n_frames, dtype=np.int8)
        pre_l = _u8_from_float01(_to_numpy(pre.field('triggers_physical').field('l')).astype(np.float32))
        pre_r = _u8_from_float01(_to_numpy(pre.field('triggers_physical').field('r')).astype(np.float32))
        post_char_field = post.field('character')
        post_char_for_guard = post_char_field.to_numpy(zero_copy_only=False)
        if isinstance(post_char_for_guard, np.ma.MaskedArray):
            post_char_for_guard = post_char_for_guard.compressed()
        require_replay_chars_in_manifest(np.asarray(post_char_for_guard), manifest_chars)
        post_char = _to_numpy(post_char_field).astype(np.uint8)
        post_state = _to_numpy(post.field('state')).astype(np.uint16)
        post_pos = post.field('position')
        post_pos_x = _to_numpy(post_pos.field('x')).astype(np.float32)
        post_pos_y = _to_numpy(post_pos.field('y')).astype(np.float32)
        post_char_id_u8[:, slot] = post_char
        post_action_id_u16[:, slot] = post_state
        post_pos_x_all[:, slot] = post_pos_x
        post_pos_y_all[:, slot] = post_pos_y
        post_pos_z = _post_position_z(post, n_frames)
        post_pos_z_all[:, slot] = post_pos_z
        post_dir = _dir_to_facing(_to_numpy(post.field('direction')).astype(np.float32))
        post_percent = _to_numpy(post.field('percent')).astype(np.float32)
        post_percent_all[:, slot] = post_percent
        post_shield = _to_numpy(post.field('shield')).astype(np.float32)
        post_shield_f32_all[:, slot] = post_shield
        post_stocks = _to_numpy(post.field('stocks')).astype(np.uint8)
        post_stocks_u8_all[:, slot] = post_stocks
        post_jumps = _to_numpy(post.field('jumps')).astype(np.uint8)
        post_airborne = _to_numpy(post.field('airborne')).astype(np.uint8)
        post_on_ground = _airborne_to_on_ground(post_airborne, n_frames)
        fighter_scale_y = np.full(n_frames, np.float32(scl), dtype=np.float32)
        post_hitlag = _u16_from_float_frames(_to_numpy(post.field('hitlag')).astype(np.float32), n_frames)
        post_hitlag_u16_all[:, slot] = post_hitlag
        post_misc_as = _to_numpy(post.field('misc_as')).astype(np.float32)
        post_state_age_f32 = _to_numpy(post.field('state_age')).astype(np.float32)
        post_state_age = _i16_from_state_age(post_state_age_f32, n_frames)
        post_state_age_all[:, slot] = post_state_age
        post_anim_frame_f32 = _f32_from_state_age(post_state_age_f32, n_frames)
        post_anim_frame_f32_all[:, slot] = post_anim_frame_f32
        hurtbox_state = _to_numpy(post.field('hurtbox_state')).astype(np.uint8)
        l_cancel = _to_numpy(post.field('l_cancel')).astype(np.uint8)
        ground_id = _to_numpy(post.field('ground')).astype(np.uint16)
        animation_index = _to_numpy(post.field('animation_index')).astype(np.uint32)
        post_animation_index_u32_all[:, slot] = animation_index
        instance_hit_by = _to_numpy(post.field('last_hit_by_instance')).astype(np.uint16)
        post_instance_hit_by_u16_all[:, slot] = instance_hit_by
        instance_id = _to_numpy(post.field('instance_id')).astype(np.uint16)
        last_attack_landed = _to_numpy(post.field('last_attack_landed')).astype(np.uint8)
        combo_count = _to_numpy(post.field('combo_count')).astype(np.uint8)
        post_combo_count_u8_all[:, slot] = combo_count
        post_last_attack_landed_u8_all[:, slot] = last_attack_landed
        last_hit_by = _to_numpy(post.field('last_hit_by')).astype(np.uint8)
        sf = post.field('state_flags')
        state_flags = np.stack([_to_numpy(sf.field('0')).astype(np.uint8), _to_numpy(sf.field('1')).astype(np.uint8), _to_numpy(sf.field('2')).astype(np.uint8), _to_numpy(sf.field('3')).astype(np.uint8), _to_numpy(sf.field('4')).astype(np.uint8)], axis=1)
        post_hitstun = hitstun_u16_from_misc_as_and_state_flags3(misc_as_f32=post_misc_as, state_flags3_u8=state_flags[:, 3], n=n_frames)
        post_state_flags_u8[:, slot, :] = state_flags
        vel = post.field('velocities')
        speed_air_x_self = _to_numpy(vel.field('self_x_air')).astype(np.float32)
        speed_y_self = _to_numpy(vel.field('self_y')).astype(np.float32)
        speed_x_attack = _to_numpy(vel.field('knockback_x')).astype(np.float32)
        speed_y_attack = _to_numpy(vel.field('knockback_y')).astype(np.float32)
        speed_ground_x_self = _to_numpy(vel.field('self_x_ground')).astype(np.float32)
        post_instance_id_slot = _to_numpy(post.field('instance_id')).astype(np.uint16)
        port_1based = int(src_ports[slot])
        dmg_x2225_b7, dmg_x2224_b2 = dmg_flags_by_port.get(port_1based, (0, 0))
        msl_binding.validation_fill_visible_player(samples.seed_u8(), samples.prev_input_u8(), samples.input_u8(), samples.ref_u8(), int(slot), int(stage_id), int(port_1based - 1), int(dmg_x2225_b7), int(dmg_x2224_b2), pre_buttons_physical, pre_main_x, pre_main_y, pre_c_x, pre_c_y, pre_l, pre_r, post_char, post_state, post_state_age, post_anim_frame_f32, post_pos_x, post_pos_y, post_pos_z, post_dir, post_percent, post_shield, post_stocks, post_jumps, post_on_ground, post_hitlag, post_hitstun, l_cancel, hurtbox_state, ground_id, animation_index, instance_hit_by, instance_id, last_attack_landed, combo_count, last_hit_by, state_flags, speed_air_x_self, speed_ground_x_self, speed_y_self, speed_x_attack, speed_y_attack)
        seed_prev_action_id, seed_prev_action_frame = derive_seed_prev_action_post(post_action_id_u16=post_state, post_action_frame_i16=post_state_age)
        samples['seed_t']['seed_prev_action_id'][:, slot] = seed_prev_action_id
        samples['seed_t']['seed_prev_action_frame'][:, slot] = seed_prev_action_frame
        samples['seed_t']['throw_pulse_consumed'][:, slot] = 0
        samples['seed_t']['throw_pulse_crossed_prev_frame'][:, slot] = 0
        samples['seed_t']['throw_command_pending_pulse_frame'][:, slot] = 0
        samples['seed_t']['source_clear_owner_set_phase'][:, slot] = 0
        samples['seed_t']['source_clear_processhit_damage_pending_phase'][:, slot] = 0
        samples['seed_t']['fighter_8006cda4_pre_gate_consume_count'][:, slot] = 0
        samples['seed_t']['source_clear_grounded_damage_clear_phase'][:, slot] = 0
        samples['seed_t']['source_clear_terminal_phase'][:, slot] = 0
        post_attackdash_x0 = _derive_attackdash_x0_seed_lane(action_id_u16=post_state, action_frame_i16=post_state_age, misc_as_f32=post_misc_as, act_attack_dash=act_attack_dash, attackdash_x0_init_frames=int(common['attackdash_x0_init_frames']))
        samples['seed_t']['attackdash_x0'][:, slot] = post_attackdash_x0[:-1]
        post_jab_x0 = np.zeros(n_frames, dtype=np.uint8)
        jab_mask = (post_state == np.uint16(act_attack_11)) | (post_state == np.uint16(act_attack_12)) | (post_state == np.uint16(act_attack_13))
        post_jab_x0[jab_mask] = (post_misc_as[jab_mask] > 0.0).astype(np.uint8)
        samples['seed_t']['jab_x0'][:, slot] = post_jab_x0[:-1]
        samples['seed_t']['jab_rapid_count'][:, slot] = _derive_jab_rapid_count_seed_lane(action_id_u16=post_state, buttons_released_u16=np.concatenate(([np.uint16(0)], pre_buttons_physical[:-1])) & ~pre_buttons_physical, buttons_pressed_u16=pre_buttons_physical & ~np.concatenate(([np.uint16(0)], pre_buttons_physical[:-1])), button_mask_a=button_mask_a)[:-1]
        port0 = int(src_ports[slot]) - 1
        match_flow_timer = _derive_match_flow_timer(action_id_u16=post_state, port0=port0, common=common)
        match_flow_timer_u8_all[:, slot] = match_flow_timer
        samples['seed_t']['match_flow_timer'][:, slot] = match_flow_timer[:-1]
        samples['seed_t']['opening_input_lock_timer'][:, slot] = _derive_opening_input_lock_timer(frame_id_i32=frame_ids)[:-1]
        samples['seed_t']['entry_end_fall_lock'][:, slot] = _derive_entry_end_fall_lock(action_id_u16=post_state, on_ground_u8=post_on_ground)[:-1]
        samples['seed_t']['camera_box_visible_x221f_b0'][:, slot] = derive_camera_box_visible_x221f_b0(state_flags_u8=state_flags)[:-1]
        samples['seed_t']['rebirth_camera_anchor_y_f32'][:, slot] = derive_rebirth_camera_anchor_y(action_id_u16=post_state, stage_id_u32=int(stage_id), respawn_point_y=_respawn_point_y_for_stage_port(stage_id=int(stage_id), port0=port0))[:-1]
        camera_target_world_x, camera_target_world_y, camera_target_world_z, camera_box_radius = derive_camera_target_world(char_id_u8=post_char, animation_index_u32=animation_index, anim_frame_f32=post_anim_frame_f32, fighter_scale_y_f32=fighter_scale_y, facing_u8=post_dir, pos_x_f32=post_pos_x, pos_y_f32=post_pos_y, pos_z_f32=post_pos_z)
        samples['seed_t']['camera_target_world_x_f32'][:, slot] = camera_target_world_x[:-1]
        samples['seed_t']['camera_target_world_y_f32'][:, slot] = camera_target_world_y[:-1]
        samples['seed_t']['camera_target_world_z_f32'][:, slot] = camera_target_world_z[:-1]
        samples['seed_t']['camera_box_radius_f32'][:, slot] = camera_box_radius[:-1]
        camera_target_inside_stage_cam_bounds = derive_camera_target_point_inside_stage_cam_bounds(stage_id_u32=int(stage_id), camera_target_world_x_f32=camera_target_world_x, camera_target_world_y_f32=camera_target_world_y, camera_box_radius_f32=camera_box_radius)
        samples['seed_t']['camera_target_point_inside_stage_cam_bounds_u8'][:, slot] = camera_target_inside_stage_cam_bounds[:-1]
        samples['seed_t']['magnify_damage_counter_x1910'][:, slot] = derive_magnify_damage_counter_x1910(action_id_u16=post_state, state_flags_u8=state_flags, camera_target_point_inside_stage_cam_bounds_u8=camera_target_inside_stage_cam_bounds, percent_f32=post_percent, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, instance_hit_by_u16=instance_hit_by, last_hit_by_u8=last_hit_by, interval_frames=int(common['magnify_damage_interval_frames']), percent_limit=int(common['magnify_damage_percent_limit']), damage_amount=int(common['magnify_damage_amount']))[:-1]
        samples['seed_t']['downwait_timer'][:, slot] = derive_downwait_timer(action_id_u16=post_state, hitstun_u16=post_hitstun, down_wait_frames=int(common['down_wait_frames']), act_down_damage_u=185, act_down_damage_d=193, act_down_wait_u=act_down_wait_u, act_down_wait_d=act_down_wait_d)[:-1]
        samples['seed_t']['passivewall_timer'][:, slot] = _derive_passivewall_timer(action_id_u16=post_state, action_frame_i16=post_state_age, common=common)[:-1]
        walljump_threshold = np.zeros(post_char.shape, dtype=np.float32)
        for char_id, threshold in char_walljump_setup_x_delta_threshold.items():
            if not bool(char_can_walljump.get(int(char_id), False)):
                continue
            walljump_threshold[post_char == np.uint8(char_id)] = np.float32(threshold)
        walljump_timer, walljump_side = _derive_walljump_phase_seed_lanes(action_id_u16=post_state, action_frame_i16=post_state_age, walljump_setup_x_delta_threshold_f32=walljump_threshold, pos_x_f32=post_pos_x, pos_y_f32=post_pos_y, raw_main_x_i8=pre_main_x)
        samples['seed_t']['walljump_input_timer'][:, slot] = walljump_timer[:-1]
        samples['seed_t']['walljump_wall_side_i8'][:, slot] = walljump_side[:-1]
        wall_kind_seed, wall_id_seed = _derive_mpcoll_wall_seed_lanes(action_id_u16=post_state, action_frame_i16=post_state_age, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, pos_x_f32=post_pos_x, pos_y_f32=post_pos_y, stage_id_u32=int(stage_id), stage_segments=stage_segments)
        samples['seed_t']['mpcoll_wall_kind_seed_u8'][:, slot] = wall_kind_seed[:-1]
        samples['seed_t']['mpcoll_wall_id_seed_u16'][:, slot] = wall_id_seed[:-1]
        specialhi_rotate_model, specialhi_rotate_model_valid = _derive_specialhi_rotate_model_seed_lane(action_id_u16=post_state, facing_u8=post_dir, pos_x_f32=post_pos_x, pos_y_f32=post_pos_y, speed_air_x_self_f32=speed_air_x_self, speed_y_self_f32=speed_y_self, stage_id_u32=int(stage_id), stage_segments=stage_segments, act_fx_special_hi=act_fx_special_hi, act_fx_special_air_hi=act_fx_special_air_hi, act_fx_special_hi_landing=act_fx_special_hi_landing, act_fx_special_hi_fall=act_fx_special_hi_fall, act_fx_special_hi_bound=act_fx_special_hi_bound)
        specialhi_rotate_model_all[:, slot] = specialhi_rotate_model
        specialhi_rotate_model_valid_all[:, slot] = specialhi_rotate_model_valid
        samples['seed_t']['specialhi_rotate_model_f32'][:, slot] = specialhi_rotate_model[:-1]
        samples['seed_t']['specialhi_rotate_model_valid_u8'][:, slot] = specialhi_rotate_model_valid[:-1]
        facing_dir1_post = _derive_facing_dir1_sign(facing_u8=post_dir, action_id_u16=post_state)
        samples['seed_t']['facing_dir1'][:, slot] = facing_dir1_post[:-1]
        common_fall_valid, common_fall_x4, common_fall_msid = _derive_common_fall_blend_seed(char_id_u8=post_char, action_id_u16=post_state, speed_air_x_self_f32=speed_air_x_self, facing_dir_f32=facing_dir1_post.astype(np.float32), air_drift_max_by_char=air_drift_max_by_char, threshold=common_fall_blend_threshold, lerp=common_fall_blend_lerp)
        samples['seed_t']['common_fall_blend_valid_u8'][:, slot] = common_fall_valid[:-1]
        samples['seed_t']['common_fall_blend_x4_f32'][:, slot] = common_fall_x4[:-1]
        samples['seed_t']['common_fall_blend_msid_u16'][:, slot] = common_fall_msid[:-1]
        samples['seed_t']['kb_smashcharge_active'][:, slot] = _derive_kb_smashcharge_active_from_post(post=post)[:-1]
        damage_time_since_hit_x18ac = derive_damage_time_since_hit_x18ac(action_id_u16=post_state, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, state_flags_u8=state_flags)
        samples['seed_t']['damage_time_since_hit_x18ac'][:, slot] = damage_time_since_hit_x18ac[:-1]
        source_clear_timer_x18c8, source_clear_owner_set_phase = _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(action_id_u16=post_state, char_id_u8=post_char, on_ground_u8=post_on_ground, state_flags_u8=state_flags, last_hit_by_u8=last_hit_by, x9_b1_by_char=action_x9_b1_by_char, source_clear_init_frames=source_clear_x18c8_init_frames)
        samples['seed_t']['source_clear_timer_x18c8'][:, slot] = source_clear_timer_x18c8[:-1]
        samples['seed_t']['source_clear_owner_set_phase'][:, slot] = source_clear_owner_set_phase[:-1]
        samples['seed_t']['source_clear_grounded_damage_clear_phase'][:, slot] = _derive_source_clear_grounded_damage_clear_phase_seed_lane(action_id_u16=post_state, action_frame_i16=post_state_age, on_ground_u8=post_on_ground, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, combo_count_u8=combo_count, source_clear_timer_x18c8_u8=source_clear_timer_x18c8, source_clear_owner_set_phase_u8=source_clear_owner_set_phase, state_flags_u8=state_flags, last_hit_by_u8=last_hit_by)[:-1]
        samples['seed_t']['source_clear_terminal_phase'][:, slot] = _derive_source_clear_terminal_phase_seed_lane(char_id_u8=post_char, action_id_u16=post_state, action_frame_i16=post_state_age, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, combo_count_u8=combo_count, last_attack_landed_u8=last_attack_landed, source_clear_timer_x18c8_u8=source_clear_timer_x18c8, source_clear_owner_set_phase_u8=source_clear_owner_set_phase, state_flags_u8=state_flags, last_hit_by_u8=last_hit_by, terminal_followup_cmd0_on_by_char_action=source_clear_followup_cmd0_on_by_char_action, terminal_followup_cmd0_off_by_char_action=source_clear_followup_cmd0_off_by_char_action)[:-1]
        colanim_x198c, colanim_x1990, colanim_x1994, colanim_x2221_b0, colanim_rebirth_fall_x1994 = derive_colanim_internals(action_id_u16=post_state, action_frame_i16=post_state_age, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, hurtbox_state_u8=hurtbox_state, colanim_throw_x1994_frames=int(common['colanim_throw_x1994_frames']), colanim_cliff_x1990_frames=int(common['colanim_cliff_x1990_frames']), colanim_damage_x1994_frames=int(common['colanim_damage_x1994_frames']), colanim_passivewall_x1990_frames=int(common['colanim_passivewall_x1990_frames']), colanim_rebirth_fall_x1994_frames=int(common['colanim_rebirth_fall_x1994_frames']), throw_actions=(act_throw_f, act_throw_b, act_throw_hi, act_throw_lw), cliff_actions=(act_cliff_catch, act_cliff_wait), passivewall_actions=(act_passive_wall, act_passive_wall_jump), damage_actions=(act_damage_hi_1, act_damage_hi_2, act_damage_hi_3, act_damage_n_1, act_damage_n_2, act_damage_n_3, act_damage_lw_1, act_damage_lw_2, act_damage_lw_3, act_damage_air_1, act_damage_air_2, act_damage_air_3, act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_damage_fall), fall_actions=(act_fall,), rebirth_actions=(act_rebirth, act_rebirth_wait))
        samples['seed_t']['colanim_hit_status_x198c'][:, slot] = colanim_x198c[:-1]
        samples['seed_t']['colanim_lock_x2221_b0'][:, slot] = colanim_x2221_b0[:-1]
        samples['seed_t']['colanim_timer_x1990'][:, slot] = colanim_x1990[:-1]
        samples['seed_t']['colanim_timer_x1994'][:, slot] = colanim_x1994[:-1]
        samples['seed_t']['colanim_rebirth_fall_x1994_seed'][:, slot] = colanim_rebirth_fall_x1994[:-1]
        samples['seed_t']['instance_id_x2073'][:, slot] = derive_instance_id_x2073(char_id_u8=post_char, action_id_u16=post_state, action_frame_i16=post_state_age, data_dir='data')[:-1]
        samples['seed_t']['zelda_twin_state_flags_2218_u8'][:, slot] = _derive_zelda_twin_state_flags_2218(char_id_u8=post_char, state_flags_u8=state_flags, zelda_internal_id=zelda_char_id)[:-1]
        samples['seed_t']['speciallw_counter_hitlag_floor_active_u8'][:, slot] = _derive_marth_counter_hitlag_floor_active(char_id_u8=post_char, action_id_u16=post_state, state_flags_u8=state_flags)[:-1]
        neutral_frame = neutral_lut[post_char]
        frame_max = frame_max_lut[post_char]
        import msl_binding
        main_x_proc, main_y_proc, c_x_proc, c_y_proc, stick_x, stick_y, cstick_y, trigger_unit, buttons_pressed, lr_press_timer, lightshield_amount, guard_setoff_hitlag_exit_phase = msl_binding.validation_derive_guard_input_prefix(samples.seed_u8(), int(slot), pre_buttons_physical, pre_main_x, pre_main_y, pre_c_x, pre_c_y, pre_l, pre_r, post_char, post_state, post_state_age, post_dir, post_shield, post_hitlag, state_flags, neutral_frame, frame_max, int(ucf_enabled), int(ucf_cardinals_1_0_enabled), float(lstick_deadzone_x), float(lstick_deadzone_y), float(guard_stick_lerp_x44c), int(button_mask_lr), int(button_mask_z), float(common['trigger_deadzone']), int(common['guard_x10_init_frames']), int(act_guard_on), int(act_guard), int(act_guard_off), int(act_guard_reflect), int(act_guard_set_off), int(common['guard_special_enable_frames']), float(common['hitlag_dmg_mul']), float(common['hitlag_base']), int(common['powershield_reflect_frames']), int(common['powershield_reflect_total_frames']))
        lightshield_amount_all[:, slot] = lightshield_amount
        frame_speed_mul = derive_frame_speed_mul_f32(state_age_f32=post_state_age_f32, action_id=post_state, hitlag=post_hitlag, char_id=post_char, animation_index=animation_index, lr_press_timer=lr_press_timer, shield_hp=post_shield, lightshield_amount=lightshield_amount, common_shield_hit_damage_mul=float(common['shield_hit_damage_mul']), common_shield_hit_damage_base=float(common['shield_hit_damage_base']), common_shield_hit_lightshield_min=float(common['shield_hit_lightshield_min']), common_shield_hit_lightshield_max=float(common['shield_hit_lightshield_max']), common_shield_stun_mul=float(common['shield_stun_mul']), common_shield_stun_base=float(common['shield_stun_base']), common_shield_stun_lightshield_min=float(common['shield_stun_lightshield_min']), common_shield_stun_lightshield_max=float(common['shield_stun_lightshield_max']), end_frames=end_frames, common_lcancel_window_frames=lcancel_window_frames, common_lcancel_lag_div=lcancel_lag_div, common_landing_fall_special_lag_frames=landing_fall_special_lag_frames, char_landing_air_lag_frames=char_landing_air_lag_frames, char_fallspecial_origin_lag=char_fallspecial_origin_lag)
        frame_speed_mul_all[:, slot] = frame_speed_mul
        smash_state, smash_frames, smash_hold, smash_saved_rate = _derive_smash_charge_seed_lanes(char_id_u8=post_char, action_id_u16=post_state, anim_frame_f32=post_anim_frame_f32, frame_speed_mul_f32=frame_speed_mul, on_ground_u8=post_on_ground, hitlag_u16=post_hitlag, hitstun_u16=post_hitstun, buttons_held_u16=pre_buttons_physical, button_mask_a=button_mask_a)
        samples['seed_t']['smash_charge_state'][:, slot] = smash_state[:-1]
        samples['seed_t']['smash_charge_frames'][:, slot] = smash_frames[:-1]
        samples['seed_t']['smash_charge_hold_frames_max'][:, slot] = smash_hold[:-1]
        samples['seed_t']['smash_charge_saved_rate_fp_q16_16'][:, slot] = smash_saved_rate[:-1]
        samples['seed_t']['frame_speed_mul_f32'][:, slot] = frame_speed_mul[:-1]
        samples['seed_t']['walk_anim_source_vel_f32'][:, slot] = _derive_walk_anim_source_vel_seed_lane(action_id_u16=post_state, char_id_u8=post_char, facing_dir1_i8=facing_dir1_post, frame_speed_mul_f32=frame_speed_mul, walk_divisors_by_char=char_walk_divisors)[:-1]
        samples['seed_t']['walk_retarget_tick_source_vel_f32'][:, slot] = _derive_walk_retarget_tick_source_vel_seed_lane(action_id_u16=post_state, char_id_u8=post_char, facing_dir1_i8=facing_dir1_post, anim_frame_f32=post_anim_frame_f32, ref_action_frame_i16=post_state_age, speed_ground_x_self_f32=speed_ground_x_self, walk_anim_source_vel_f32=samples['seed_t']['walk_anim_source_vel_f32'][:, slot], walk_divisors_by_char=char_walk_divisors, walk_max_by_char=char_walk_max, walk_mid_vel_mul=float(common['walk_mid_vel_mul']), walk_fast_vel_mul=float(common['walk_fast_vel_mul']), end_frames=end_frames)[:-1]
        samples['seed_t']['run_anim_source_vel_f32'][:, slot] = _derive_run_anim_source_vel_seed_lane(action_id_u16=post_state, char_id_u8=post_char, facing_dir1_i8=facing_dir1_post, frame_speed_mul_f32=frame_speed_mul, run_scaling_by_char=char_run_scaling)[:-1]
        turn_kneebend_face = np.zeros(n_frames - 1, dtype=np.uint8)
        turn_kneebend_hidden_face = (post_state[:-1] == np.uint16(act_turn)) & (post_state[1:] == np.uint16(act_kneebend)) & (post_state_age[:-1] == np.int16(1)) & (post_dir[1:] != post_dir[:-1])
        turn_kneebend_face[turn_kneebend_hidden_face] = (post_dir[1:][turn_kneebend_hidden_face] + 1).astype(np.uint8)
        samples['seed_t']['turn_kneebend_facing_override_u8'][:, slot] = turn_kneebend_face
        is_dash = post_state == np.uint16(act_dash)
        dash_entry = is_dash & ~np.concatenate(([False], is_dash[:-1]))
        is_jump_ground = (post_state == np.uint16(act_jump_f)) | (post_state == np.uint16(act_jump_b))
        is_jump_aerial = (post_state == np.uint16(act_jump_aerial_f)) | (post_state == np.uint16(act_jump_aerial_b))
        is_jump = (post_state == np.uint16(act_jump_f)) | (post_state == np.uint16(act_jump_b)) | (post_state == np.uint16(act_jump_aerial_f)) | (post_state == np.uint16(act_jump_aerial_b))
        prev_state = np.concatenate(([post_state[0]], post_state[:-1]))
        pre_input_jump_entry = is_jump_ground & (post_state != prev_state)
        jump_entry = is_jump_aerial & (post_state != prev_state)
        fastfall_ok = is_jump | (post_state == np.uint16(act_fall)) | (post_state == np.uint16(act_fall_f)) | (post_state == np.uint16(act_fall_b)) | (post_state == np.uint16(act_fall_aerial)) | (post_state == np.uint16(act_fall_aerial_f)) | (post_state == np.uint16(act_fall_aerial_b)) | (post_state == np.uint16(act_fall_special)) | (post_state == np.uint16(act_fall_special_f)) | (post_state == np.uint16(act_fall_special_b)) | (post_state == np.uint16(act_damage_fall)) | (post_state == np.uint16(act_attack_air_n)) | (post_state == np.uint16(act_attack_air_f)) | (post_state == np.uint16(act_attack_air_b)) | (post_state == np.uint16(act_attack_air_hi)) | (post_state == np.uint16(act_attack_air_lw)) | (post_state == np.uint16(act_escape_air))
        damage_sdi_reset_post = derive_damage_hitlag_sdi_reset_post_mask(action_id=post_state, hitlag_u16=post_hitlag, state_flags_u8=state_flags, pos_x=post_pos_x, pos_y=post_pos_y, stick_x_unit=stick_x, stick_y_unit=stick_y, damage_actions=(act_damage_hi_1, act_damage_hi_2, act_damage_hi_3, act_damage_n_1, act_damage_n_2, act_damage_n_3, act_damage_lw_1, act_damage_lw_2, act_damage_lw_3, act_damage_air_1, act_damage_air_2, act_damage_air_3, act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_fly_reflect_wall, act_fly_reflect_ceil, act_damage_fall, act_down_damage_d), sdi_step_mul=float(common['sdi_step_mul']))
        damage_entry_reset_post = derive_damage_entry_tilt_timer_reset_post_mask(action_id=post_state, action_frame=post_state_age, hitlag_u16=post_hitlag, percent=post_percent, instance_hit_by=instance_hit_by, damage_actions=(act_damage_hi_1, act_damage_hi_2, act_damage_hi_3, act_damage_n_1, act_damage_n_2, act_damage_n_3, act_damage_lw_1, act_damage_lw_2, act_damage_lw_3, act_damage_air_1, act_damage_air_2, act_damage_air_3, act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_fly_reflect_wall, act_fly_reflect_ceil, act_damage_fall, act_down_damage_d))
        damage_tilt_timer_reset_post = damage_sdi_reset_post | damage_entry_reset_post
        import msl_binding
        turn_frames = turn_frames_lut[post_char]
        tilt_timer_y_pre, tilt_timer_y_post, fall_fast_post, turn_has_turned = msl_binding.validation_derive_input_history_suffix(samples.seed_u8(), int(slot), pre_buttons_physical, pre_main_x, pre_main_y, stick_x, stick_y, cstick_y, trigger_unit, buttons_pressed, post_state, post_state_age, post_dir, post_hitlag, speed_y_self, post_on_ground, damage_tilt_timer_reset_post, float(lstick_tilt_x_thresh), float(lstick_tilt_y_thresh), float(fastfall_stick_threshold), int(fastfall_tilt_max_frames), float(tap_jump_threshold), int(tap_jump_tilt_max_frames), float(dash_run_jump_stick_y_threshold), float(tap_jump_release_threshold), float(dash_flick_abs), int(dash_flick_tilt_max_frames), turn_frames, float(common['powershield_reflect_trigger_min']), int(button_mask_a), int(button_mask_b), int(button_mask_xy), int(button_mask_dpad_up), int(button_mask_dpad_down), int(button_mask_lr), int(button_mask_z), int(act_guard_reflect), int(act_kneebend), int(act_dash), int(act_run), int(act_run_direct), int(act_run_brake), int(act_turn_run), int(act_turn), int(act_jump_f), int(act_jump_b), int(act_jump_aerial_f), int(act_jump_aerial_b), int(act_fall), int(act_fall_f), int(act_fall_b), int(act_fall_aerial), int(act_fall_aerial_f), int(act_fall_aerial_b), int(act_fall_special), int(act_fall_special_f), int(act_fall_special_b), int(act_damage_fall), int(act_attack_air_n), int(act_attack_air_f), int(act_attack_air_b), int(act_attack_air_hi), int(act_attack_air_lw), int(act_escape_air), int(multijump_first_lut[post_char[0]]), int(multijump_count_lut[post_char[0]]))
        post_turn_has_turned_u8[:, slot] = turn_has_turned
        run_x0 = derive_run_x0(action_id=post_state, hitlag_u16=post_hitlag, run_x0_init_x430=float(common['run_x0_init_x430']), act_run=act_run, act_run_direct=act_run_direct, act_turn_run=act_turn_run)
        samples['seed_t']['run_x0'][:, slot] = run_x0[:-1]
        runbrake_cmd0 = derive_runbrake_cmd0(action_id_u16=post_state, anim_frame_f32=post_anim_frame_f32, char_id_u8=post_char, cmd0_on_by_char=runbrake_cmd0_on_by_char, cmd0_off_by_char=runbrake_cmd0_off_by_char, act_run_brake=act_run_brake)
        samples['seed_t']['runbrake_cmd0'][:, slot] = runbrake_cmd0[:-1]
        dash_x4 = derive_dash_x4(action_id_u16=post_state, action_frame_i16=post_state_age, act_dash=act_dash, act_turn=act_turn)
        samples['seed_t']['dash_x4'][:, slot] = dash_x4[:-1]
        shine_release_lag, shine_is_release = derive_shine_release_state(action_id_u16=post_state, action_frame_i16=post_state_age, buttons_held_u16=pre_buttons_physical, hitlag_u16=post_hitlag, release_lag_init_u8=reflector_release_lag_lut[post_char], button_mask_b=button_mask_b)
        samples['seed_t']['shine_release_lag'][:, slot] = shine_release_lag[:-1]
        samples['seed_t']['shine_is_release'][:, slot] = shine_is_release[:-1]
        ecb_lock_timer = derive_ecb_lock_timer(on_ground_u8=post_on_ground, action_id_u16=post_state, lock_frames_ground_to_air=10)
        samples['seed_t']['ecb_lock_timer'][:, slot] = ecb_lock_timer[:-1]
        ecb_lock_bottom_rel_y, ecb_lock_bottom_rel_y_valid = derive_ecb_lock_bottom_rel_y(char_id_u8=post_char, action_id_u16=post_state, animation_index_u32=animation_index, anim_frame_f32=post_anim_frame_f32, on_ground_u8=post_on_ground, ecb_lock_timer_u8=ecb_lock_timer, act_jump_aerial_f=act_jump_aerial_f, act_jump_aerial_b=act_jump_aerial_b)
        samples['seed_t']['ecb_lock_bottom_rel_y_f32'][:, slot] = ecb_lock_bottom_rel_y[:-1]
        samples['seed_t']['ecb_lock_bottom_rel_y_valid_u8'][:, slot] = ecb_lock_bottom_rel_y_valid[:-1]
        damage_hitlag_ecb_bottom, damage_hitlag_ecb_top, damage_hitlag_ecb_left, damage_hitlag_ecb_right, damage_hitlag_ecb_side, damage_hitlag_ecb_valid = derive_damage_hitlag_colldata_ecb(char_id_u8=post_char, action_id_u16=post_state, animation_index_u32=animation_index, anim_frame_f32=post_anim_frame_f32, frame_speed_mul_f32=frame_speed_mul, facing_u8=post_dir, on_ground_u8=post_on_ground, hitlag_u16=post_hitlag)
        samples['seed_t']['damage_hitlag_ecb_bottom_rel_y_f32'][:, slot] = damage_hitlag_ecb_bottom[:-1]
        samples['seed_t']['damage_hitlag_ecb_top_rel_y_f32'][:, slot] = damage_hitlag_ecb_top[:-1]
        samples['seed_t']['damage_hitlag_ecb_left_rel_x_f32'][:, slot] = damage_hitlag_ecb_left[:-1]
        samples['seed_t']['damage_hitlag_ecb_right_rel_x_f32'][:, slot] = damage_hitlag_ecb_right[:-1]
        samples['seed_t']['damage_hitlag_ecb_side_rel_y_f32'][:, slot] = damage_hitlag_ecb_side[:-1]
        samples['seed_t']['damage_hitlag_ecb_valid_u8'][:, slot] = damage_hitlag_ecb_valid[:-1]
        damage_jump_buffer_x14 = derive_damage_jump_buffer_x14(action_id=post_state, hitstun_u16=post_hitstun, hitlag_u16=post_hitlag, buttons_pressed=buttons_pressed, stick_y_unit=stick_y, tilt_timer_y=tilt_timer_y_post, tap_jump_threshold=tap_jump_threshold, tap_jump_tilt_max_frames=tap_jump_tilt_max_frames, button_mask_xy=button_mask_xy, damage_actions=(act_damage_hi_1, act_damage_hi_2, act_damage_hi_3, act_damage_n_1, act_damage_n_2, act_damage_n_3, act_damage_lw_1, act_damage_lw_2, act_damage_lw_3, act_damage_air_1, act_damage_air_2, act_damage_air_3, act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_fly_reflect_wall, act_fly_reflect_ceil, act_damage_fall))
        samples['seed_t']['damage_jump_buffer_x14'][:, slot] = damage_jump_buffer_x14[:-1]
        damage_post_hitlag_cb_kind = derive_damage_post_hitlag_cb_kind(action_id=post_state, hitstun_u16=post_hitstun, damage_actions=(act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_damage_fall))
        samples['seed_t']['damage_post_hitlag_cb_kind'][:, slot] = damage_post_hitlag_cb_kind[:-1]
        ledge_cooldown = _derive_ledge_cooldown(action_id_u16=post_state, hitlag_u16=post_hitlag, common=common)
        samples['seed_t']['ledge_cooldown'][:, slot] = ledge_cooldown[:-1]
        cliff_ledge_floor_segment_id = _derive_cliff_ledge_floor_segment_id(action_id_u16=post_state, facing_u8=post_dir, on_ground_u8=post_on_ground, ledge_cooldown_u8=ledge_cooldown, stage_id=int(stage_id), data_root=data_root)
        samples['seed_t']['cliff_ledge_floor_segment_id_u16'][:, slot] = cliff_ledge_floor_segment_id[:-1]
        cliff_option_stick_latch_x8 = _derive_cliff_option_stick_latch_x8(action_id_u16=post_state, main_x_i8=main_x_proc, main_y_i8=main_y_proc, c_x_i8=c_x_proc, c_y_i8=c_y_proc, common=common)
        samples['seed_t']['cliff_option_stick_latch_x8'][:, slot] = cliff_option_stick_latch_x8[:-1]
        landing_fallspecial_allow_interrupt = _derive_landing_fallspecial_allow_interrupt_seed_lane(action_id_u16=post_state, char_id_u8=post_char, origin_allow_by_char=char_fallspecial_origin_allow_interrupt)
        post_landing_fallspecial_allow_interrupt[:, slot] = landing_fallspecial_allow_interrupt
        samples['seed_t']['landing_fallspecial_allow_interrupt'][:, slot] = landing_fallspecial_allow_interrupt[:-1]
    import msl_binding
    hist_attack_id, hist_attack_instance, hist_stale_queue_index, hist_stale_move_id, hist_stale_attack_instance = msl_binding.validation_derive_staling_buffers(samples.seed_u8(), samples.ref_u8(), _structured_rows_as_writable_bytes(items_fixed), post_anim_frame_f32_all, np.asarray(_laser_shot_item_kinds(Path('data') / 'items' / 'lasers.bin'), dtype=np.uint16), int(num_players))
    hist = SimpleNamespace(attack_id=hist_attack_id, attack_instance=hist_attack_instance, stale_queue_index=hist_stale_queue_index, stale_move_id=hist_stale_move_id, stale_attack_instance=hist_stale_attack_instance)
    shield_hit_mul = float(common['shield_hit_damage_mul'])
    shield_hit_base = float(common['shield_hit_damage_base'])
    shield_hit_ls_min = float(common['shield_hit_lightshield_min'])
    shield_hit_ls_max = float(common['shield_hit_lightshield_max'])
    shield_hold_drain_mul = float(common['shield_hold_drain_mul'])
    shield_hold_drain_base = float(common['shield_hold_drain_base'])
    shield_hold_drain_max = float(common['shield_hold_drain_max'])
    shield_stun_mul = float(common['shield_stun_mul'])
    shield_stun_base = float(common['shield_stun_base'])
    shield_stun_ls_min = float(common['shield_stun_lightshield_min'])
    shield_stun_ls_max = float(common['shield_stun_lightshield_max'])
    max_end_msid = max((max(v.keys(), default=0) for v in end_frames.by_char_id.values()), default=40)
    end_frame_lut = np.zeros((256, max(max_end_msid, 40) + 1), dtype=np.float32)
    for cid, by_msid in end_frames.by_char_id.items():
        for msid, end_frame in by_msid.items():
            end_frame_lut[int(cid) & 255, int(msid)] = np.float32(float(end_frame))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_guardsetoff_frame_speed_overrides is required; run `make build`') from exc
    guardsetoff_rate = msl_binding.derive_guardsetoff_frame_speed_overrides(_ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_hitlag_u16_all, dtype=np.uint16), _ascontiguousarray(post_state_age_all, dtype=np.int16), _ascontiguousarray(post_animation_index_u32_all, dtype=np.uint32), _ascontiguousarray(post_char_id_u8, dtype=np.uint8), _ascontiguousarray(post_state_flags_u8, dtype=np.uint8), _ascontiguousarray(post_shield_f32_all, dtype=np.float32), _ascontiguousarray(lightshield_amount_all, dtype=np.float32), _ascontiguousarray(hist.attack_id, dtype=np.uint16), _ascontiguousarray(hist.stale_queue_index, dtype=np.uint8), _ascontiguousarray(hist.stale_move_id, dtype=np.uint16), manifest_tables.active_shield_hit_lut, end_frame_lut, np.asarray(stale_weights, dtype=np.float32), int(num_players), int(act_guard_set_off), shield_hit_mul, shield_hit_base, shield_hit_ls_min, shield_hit_ls_max, shield_stun_mul, shield_stun_base, shield_stun_ls_min, shield_stun_ls_max)
    guardsetoff_rate_mask = guardsetoff_rate[:, :num_players] > np.float32(0.0)
    frame_speed_mul_all[:, :num_players][guardsetoff_rate_mask] = guardsetoff_rate[:, :num_players][guardsetoff_rate_mask]
    samples['seed_t']['frame_speed_mul_f32'][:, :num_players] = frame_speed_mul_all[:-1, :num_players]
    guard_setoff_exit_frame_speed = msl_binding.derive_guard_setoff_exit_frame_speed_seed_lane(
        _ascontiguousarray(post_action_id_u16, dtype=np.uint16),
        _ascontiguousarray(post_hitlag_u16_all, dtype=np.uint16),
        _ascontiguousarray(frame_speed_mul_all, dtype=np.float32),
        int(num_players),
        int(act_guard_set_off),
    )
    samples['seed_t']['guard_setoff_exit_frame_speed_mul_f32'][:, :num_players] = guard_setoff_exit_frame_speed[:-1, :num_players]
    for slot in range(num_players):
        samples['seed_t']['source_clear_processhit_damage_pending_phase'][:, slot] = _derive_source_clear_processhit_damage_pending_phase_seed_lane(action_id_u16=samples['seed_t']['action_id'][:, slot], action_frame_i16=samples['seed_t']['action_frame'][:, slot], on_ground_u8=samples['seed_t']['on_ground'][:, slot], hitlag_u16=samples['seed_t']['hitlag'][:, slot], hitstun_u16=samples['seed_t']['hitstun'][:, slot], combo_count_u8=samples['seed_t']['combo_count'][:, slot], last_attack_landed_u8=samples['seed_t']['last_attack_landed'][:, slot], source_clear_timer_x18c8_u8=samples['seed_t']['source_clear_timer_x18c8'][:, slot], source_clear_owner_set_phase_u8=samples['seed_t']['source_clear_owner_set_phase'][:, slot], colanim_hit_status_x198c_u8=samples['seed_t']['colanim_hit_status_x198c'][:, slot], state_flags_u8=samples['seed_t']['state_flags'][:, slot, :], last_hit_by_u8=samples['seed_t']['last_hit_by'][:, slot])
    import msl_binding
    msl_binding.validation_derive_fighter_8006cda4_buffers(samples.seed_u8(), samples.ref_u8(), float(common['damagefly_roll_prob']), int(num_players), int(int(stage_id) == 2))
    illusion_ghost_pos0_x, illusion_ghost_pos0_y, illusion_ghost_pos1_x, illusion_ghost_pos1_y, illusion_ghost_pos2_x, illusion_ghost_pos2_y = derive_illusion_ghost_pos012(post_action_id_u16=post_action_id_u16, post_action_frame_i16=post_state_age_all, post_pos_x=post_pos_x_all, post_pos_y=post_pos_y_all)
    samples['seed_t']['illusion_ghost_pos0_x'][:, :num_players] = illusion_ghost_pos0_x[:-1, :num_players]
    samples['seed_t']['illusion_ghost_pos0_y'][:, :num_players] = illusion_ghost_pos0_y[:-1, :num_players]
    samples['seed_t']['illusion_ghost_pos1_x'][:, :num_players] = illusion_ghost_pos1_x[:-1, :num_players]
    samples['seed_t']['illusion_ghost_pos1_y'][:, :num_players] = illusion_ghost_pos1_y[:-1, :num_players]
    samples['seed_t']['illusion_ghost_pos2_x'][:, :num_players] = illusion_ghost_pos2_x[:-1, :num_players]
    samples['seed_t']['illusion_ghost_pos2_y'][:, :num_players] = illusion_ghost_pos2_y[:-1, :num_players]
    seed_action_id = samples['seed_t']['action_id']
    seed_attack_id = samples['seed_t']['attack_id']
    seed_anim_frame_f32 = samples['seed_t']['anim_frame_f32']
    seed_animation_index = samples['seed_t']['animation_index']
    seed_on_ground = samples['seed_t']['on_ground']
    seed_hitlag = samples['seed_t']['hitlag']
    seed_pos_x = samples['seed_t']['pos_x']
    seed_char_id = samples['seed_t']['char_id']
    seed_ground_friction_mul = samples['seed_t']['ground_friction_mul']
    seed_lightshield_amount = samples['seed_t']['lightshield_amount']
    seed_guard_setoff_hitlag_damage_min = samples['seed_t']['guard_setoff_hitlag_damage_min']
    seed_stale_queue_index = samples['seed_t']['stale_queue_index']
    seed_stale_move_id = samples['seed_t']['stale_move_id']
    shield_kb_mul = float(common['shield_attacker_ground_kb_mul'])
    shield_kb_base = float(common['shield_attacker_ground_kb_base'])
    shield_kb_friction_mul = float(common['shield_attacker_ground_friction_mul'])
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_attacker_shield_ground_kb_vel is required; run `make build`') from exc
    attacker_shield_ground_kb_vel = msl_binding.derive_attacker_shield_ground_kb_vel(_ascontiguousarray(seed_action_id, dtype=np.uint16), _ascontiguousarray(seed_attack_id, dtype=np.uint16), _ascontiguousarray(seed_anim_frame_f32, dtype=np.float32), _ascontiguousarray(seed_animation_index, dtype=np.uint32), _ascontiguousarray(seed_on_ground, dtype=np.uint8), _ascontiguousarray(seed_hitlag, dtype=np.uint16), _ascontiguousarray(seed_pos_x, dtype=np.float32), _ascontiguousarray(seed_char_id, dtype=np.uint8), _ascontiguousarray(seed_ground_friction_mul, dtype=np.float32), _ascontiguousarray(seed_lightshield_amount, dtype=np.float32), _ascontiguousarray(seed_guard_setoff_hitlag_damage_min, dtype=np.uint8), _ascontiguousarray(seed_stale_queue_index, dtype=np.uint8), _ascontiguousarray(seed_stale_move_id, dtype=np.uint16), manifest_tables.active_shield_hit_lut, manifest_tables.char_gr_friction_lut, np.asarray(stale_weights, dtype=np.float32), int(num_players), int(act_guard_set_off), int(act_guard_reflect), shield_kb_mul, shield_kb_base, shield_kb_friction_mul)
    samples['seed_t']['attacker_shield_ground_kb_vel'] = attacker_shield_ground_kb_vel
    items_seed = None
    import msl_binding
    illusion_lut = _u8_lut_from_items(tuple((int(k) for k in illusion_item_kinds)))
    msl_binding.validation_copy_item_rows_with_illusion(samples.seed_u8(), samples.ref_u8(), _structured_rows_as_bytes(items_fixed), _ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_hitlag_u16_all, dtype=np.uint16), _ascontiguousarray(post_instance_hit_by_u16_all, dtype=np.uint16), _ascontiguousarray(illusion_ghost_pos1_x, dtype=np.float32), _ascontiguousarray(illusion_ghost_pos1_y, dtype=np.float32), illusion_lut, int(num_players))
    msl_binding.validation_derive_item_reflect_damage_mul_buffers(samples.seed_u8(), _structured_rows_as_bytes(items_fixed), _ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_char_id_u8, dtype=np.uint8), _ascontiguousarray(post_state_flags_u8, dtype=np.uint8), _ascontiguousarray(reflector_damage_mul_lut, dtype=np.float32), float(common['powershield_reflect_damage_mul']), int(num_players))
    yoshi_params = _yoshi_shyguy_params()
    if int(stage_id) == int(yoshi_params.stage_id):
        yoshi_common = _item_common_params()
        msl_binding.validation_derive_yoshi_shyguy_buffers(samples.seed_u8(), _structured_rows_as_bytes(items_fixed), _ascontiguousarray(frame_pre_random_seed, dtype=np.uint32), np.asarray(yoshi_params.vpos, dtype=np.float32), np.asarray(yoshi_params.speed, dtype=np.float32), np.asarray(yoshi_params.dyn_y_vel, dtype=np.float32), int(stage_id), int(yoshi_params.stage_id), int(yoshi_params.item_kind), int(yoshi_params.timer_reset), int(yoshi_params.spawn_delay_step), float(yoshi_params.state4_speed_mul), float(yoshi_common['item_hitlag_damage_mul']), float(yoshi_common['item_hitlag_base']))
    fox_attrs = _load_character_attrs(data_root, 'fox')
    falco_attrs = _load_character_attrs(data_root, 'falco')
    illusion_end_angle_by_char = {int(char_fox): int(fox_attrs['illusion_item_state1_angle']), int(char_falco): int(falco_attrs['illusion_item_state1_angle'])}
    source_port0_by_slot = np.array([int(p) - 1 for p in src_ports], dtype=np.uint8)
    damage_action_family = (act_damage_hi_1, act_damage_hi_2, act_damage_hi_3, act_damage_n_1, act_damage_n_2, act_damage_n_3, act_damage_lw_1, act_damage_lw_2, act_damage_lw_3, act_damage_air_1, act_damage_air_2, act_damage_air_3, act_damage_fly_hi, act_damage_fly_n, act_damage_fly_lw, act_damage_fly_top, act_damage_fly_roll, act_damage_fall)
    for slot in range(num_players):
        source_angle = np.full(samples.shape[0], np.uint16(65535), dtype=np.uint16)
        last_hit_by_seed = samples['seed_t']['last_hit_by'][:, slot].astype(np.uint8)
        for src_slot in range(num_players):
            source_mask = last_hit_by_seed == source_port0_by_slot[src_slot]
            if not np.any(source_mask):
                continue
            src_action = samples['seed_t']['action_id'][:, src_slot].astype(np.uint16)
            src_char = samples['seed_t']['char_id'][:, src_slot].astype(np.uint8)
            for char_id, angle in illusion_end_angle_by_char.items():
                char_mask = source_mask & (src_char == np.uint8(char_id))
                if not np.any(char_mask):
                    continue
                end_mask = char_mask & ((src_action == np.uint16(act_fx_special_s_end)) | (src_action == np.uint16(act_fx_special_air_s_end)))
                source_angle[end_mask] = np.uint16(angle)
        damage_meteor_cancel_x1a = derive_damage_meteor_cancel_x1a(action_id=samples['seed_t']['action_id'][:, slot], hitstun_u16=samples['seed_t']['hitstun'][:, slot], source_angle_u16=source_angle, angle_min_deg=int(common['damage_meteor_cancel_angle_min_deg']), angle_max_deg=int(common['damage_meteor_cancel_angle_max_deg']), damage_actions=damage_action_family)
        samples['seed_t']['damage_post_hitlag_cb_kind'][:, slot] = np.bitwise_or(samples['seed_t']['damage_post_hitlag_cb_kind'][:, slot].astype(np.uint8), np.left_shift(damage_meteor_cancel_x1a.astype(np.uint8), np.uint8(7))).astype(np.uint8)
    throw_pulse_consumed, throw_pulse_crossed_prev, throw_command_pending_pulse_frame = _derive_throw_pulse_seed_lanes(seed_action_id_u16=samples['seed_t']['action_id'], seed_char_id_u8=samples['seed_t']['char_id'], seed_anim_frame_f32=samples['seed_t']['anim_frame_f32'], seed_frame_speed_mul_f32=samples['seed_t']['frame_speed_mul_f32'], seed_hitstun_u16=samples['seed_t']['hitstun'], seed_last_attack_landed_u8=samples['seed_t']['last_attack_landed'], seed_last_hit_by_u8=samples['seed_t']['last_hit_by'], seed_items=samples['seed_t']['items'], num_players=num_players, pulse_frames_by_char_action=throw_pulse_frames_by_char_action, cmd1_start_by_char_action=throw_cmd1_start_by_char_action, shot_itkind_by_char=throw_shot_itkind_by_char, act_throw_b=int(act_throw_b), act_throw_hi=int(act_throw_hi), act_damage_fly_top=int(act_damage_fly_top), falco_char_id=int(char_falco))
    samples['seed_t']['throw_pulse_consumed'] = throw_pulse_consumed
    samples['seed_t']['throw_pulse_crossed_prev_frame'] = throw_pulse_crossed_prev
    samples['seed_t']['throw_command_pending_pulse_frame'] = throw_command_pending_pulse_frame
    post_team_id = np.zeros((n_frames, 4), dtype=np.uint8)
    post_char_id = np.zeros((n_frames, 4), dtype=np.uint8)
    post_action_id = np.zeros((n_frames, 4), dtype=np.uint16)
    post_action_frame = np.zeros((n_frames, 4), dtype=np.int16)
    post_anim_frame = np.zeros((n_frames, 4), dtype=np.float32)
    post_animation_index = np.zeros((n_frames, 4), dtype=np.uint32)
    post_facing = np.zeros((n_frames, 4), dtype=np.uint8)
    post_on_ground = np.zeros((n_frames, 4), dtype=np.uint8)
    post_ground_id = np.zeros((n_frames, 4), dtype=np.uint16)
    post_pos_x = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_y = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_z_2d = np.zeros((n_frames, 4), dtype=np.float32)
    post_scale_y = np.ones((n_frames, 4), dtype=np.float32)
    post_guard_tilt_x8 = np.zeros((n_frames, 4), dtype=np.uint16)
    post_guard_tilt_x4 = np.zeros((n_frames, 4), dtype=np.float32)
    post_stocks = np.zeros((n_frames, 4), dtype=np.uint8)
    post_shield_hp = np.zeros((n_frames, 4), dtype=np.float32)
    post_hurtbox_state = np.zeros((n_frames, 4), dtype=np.uint8)
    post_instance_hit_by = np.zeros((n_frames, 4), dtype=np.uint16)
    post_instance_id = np.zeros((n_frames, 4), dtype=np.uint16)
    post_hitlag = np.zeros((n_frames, 4), dtype=np.uint16)
    post_hitstun = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_flags = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    post_last_hit_by = np.full((n_frames, 4), 255, dtype=np.uint8)
    post_source_port0 = np.full((n_frames, 4), 255, dtype=np.uint8)
    pre_buttons = np.zeros((n_frames, 4), dtype=np.uint16)
    pre_main_x_2d = np.zeros((n_frames, 4), dtype=np.int8)
    pre_main_y_2d = np.zeros((n_frames, 4), dtype=np.int8)
    pre_l = np.zeros((n_frames, 4), dtype=np.uint8)
    pre_r = np.zeros((n_frames, 4), dtype=np.uint8)
    import msl_binding
    post_anim_frame = post_anim_frame_f32_all
    post_pos_z_2d = post_pos_z_all
    msl_binding.validation_project_post_cache(samples.seed_u8(), samples.prev_input_u8(), samples.input_u8(), samples.ref_u8(), post_team_id, post_char_id, post_action_id, post_action_frame, post_animation_index, post_facing, post_on_ground, post_ground_id, post_pos_x, post_pos_y, post_stocks, post_shield_hp, post_hurtbox_state, post_instance_hit_by, post_instance_id, post_hitlag, post_hitstun, post_state_flags, post_last_hit_by, post_source_port0, pre_buttons, pre_main_x_2d, pre_main_y_2d, pre_l, pre_r, int(num_players))
    hidden_pos_z = _derive_grounded_overlap_hidden_pos_z(num_players=num_players, char_id_u8=post_char_id, action_id_u16=post_action_id, on_ground_u8=post_on_ground, stocks_u8=post_stocks, pos_x_f32=post_pos_x, pos_z_f32=post_pos_z_2d, facing_u8=post_facing, common=common, data_dir='data')
    samples['seed_t']['pos_z'] = hidden_pos_z[:-1]
    if int(stage_id) == 2:
        fod_height, fod_valid, fod_velocity, fod_velocity_valid, fod_height_source = _fod_platform_motion_with_ground_contact(samples['seed_t']['stage_fod_platform_height_f32'], samples['seed_t']['stage_fod_platform_height_valid_u8'], event_fresh_u8=None if fod_fresh is None else fod_fresh[:-1], post_on_ground_u8=post_on_ground[:-1, :num_players], post_ground_id_u16=post_ground_id[:-1, :num_players], post_pos_y_f32=post_pos_y[:-1, :num_players], next_post_on_ground_u8=post_on_ground[1:, :num_players], next_post_ground_id_u16=post_ground_id[1:, :num_players], next_post_pos_y_f32=post_pos_y[1:, :num_players], line_transforms=_fod_platform_height_transform_records(data_root), motion_params=fod_motion_params, return_source=True)
        fod_deferred_velocity = np.zeros_like(fod_velocity, dtype=np.float32)
        fod_deferred_velocity_valid = np.zeros_like(fod_velocity_valid, dtype=np.uint8)
        if fod_motion_params:
            min_stage_speed = min(abs(float(fod_motion_params['down_speed'])), abs(float(fod_motion_params['up_speed'])))
            velocity_threshold = np.float32(0.5 * min_stage_speed)
            for platform_id in range(2):
                same_step = fod_height_source[:, platform_id] & np.uint8(4) != 0
                no_current_velocity = fod_velocity_valid[:, platform_id] == 0
                rows = same_step & no_current_velocity
                if fod_velocity.shape[0] > 1:
                    next_valid = np.zeros(fod_velocity.shape[0], dtype=np.uint8)
                    next_value = np.zeros(fod_velocity.shape[0], dtype=np.float32)
                    next_valid[:-1] = fod_velocity_valid[1:, platform_id]
                    next_value[:-1] = fod_velocity[1:, platform_id]
                    use_next = rows & (next_valid != 0) & (np.abs(next_value) >= velocity_threshold)
                    fod_deferred_velocity[use_next, platform_id] = next_value[use_next]
                    fod_deferred_velocity_valid[use_next, platform_id] = np.uint8(1)
                if fod_velocity.shape[0] > 2:
                    next2_valid = np.zeros(fod_velocity.shape[0], dtype=np.uint8)
                    next2_value = np.zeros(fod_velocity.shape[0], dtype=np.float32)
                    next2_valid[:-2] = fod_velocity_valid[2:, platform_id]
                    next2_value[:-2] = fod_velocity[2:, platform_id]
                    need_next2 = rows & (fod_deferred_velocity_valid[:, platform_id] == 0) & (next2_valid != 0) & (np.abs(next2_value) >= velocity_threshold)
                    fod_deferred_velocity[need_next2, platform_id] = next2_value[need_next2]
                    fod_deferred_velocity_valid[need_next2, platform_id] = np.uint8(1)
        samples['seed_t']['stage_fod_platform_height_f32'] = fod_height
        samples['seed_t']['stage_fod_platform_height_valid_u8'] = fod_valid
        samples['seed_t']['stage_fod_platform_velocity_f32'] = fod_velocity
        samples['seed_t']['stage_fod_platform_velocity_valid_u8'] = fod_velocity_valid
        samples['seed_t']['stage_fod_platform_deferred_velocity_f32'] = fod_deferred_velocity
        samples['seed_t']['stage_fod_platform_deferred_velocity_valid_u8'] = fod_deferred_velocity_valid
        fod_hidden_return_timer, fod_hidden_return_valid = _fod_hidden_return_timers(fod_height, fod_valid, motion_params=fod_motion_params)
        samples['seed_t']['stage_fod_platform_hidden_return_timer_u16'] = fod_hidden_return_timer
        samples['seed_t']['stage_fod_platform_hidden_return_valid_u8'] = fod_hidden_return_valid
        fod_visible_choice_timer, fod_visible_choice_valid, fod_visible_choice_rng_seed = _fod_visible_choice_lanes(fod_height, fod_valid, None if fod_fresh is None else fod_fresh[:-1], samples['seed_t']['frame_pre_random_seed'], motion_params=fod_motion_params)
        samples['seed_t']['stage_fod_platform_visible_choice_timer_u16'] = fod_visible_choice_timer
        samples['seed_t']['stage_fod_platform_visible_choice_valid_u8'] = fod_visible_choice_valid
        samples['seed_t']['stage_fod_platform_visible_choice_rng_seed_u32'] = fod_visible_choice_rng_seed
        samples['seed_t']['stage_fod_platform_height_source_u8'] = fod_height_source
        fod_floor_skip = _derive_fod_floor_skip_segments(action_id_u16=samples['seed_t']['action_id'][:, :num_players], action_frame_u16=samples['seed_t']['action_frame'][:, :num_players], char_id_u8=samples['seed_t']['char_id'][:, :num_players], on_ground_u8=samples['seed_t']['on_ground'][:, :num_players], pos_x_f32=samples['seed_t']['pos_x'][:, :num_players], pos_y_f32=samples['seed_t']['pos_y'][:, :num_players], speed_y_self_f32=samples['seed_t']['speed_y_self'][:, :num_players], speed_y_attack_f32=samples['seed_t']['speed_y_attack'][:, :num_players], prev_main_y_i8=samples['prev_input_t']['p']['main_y'][:, :num_players], main_y_i8=samples['input_t']['p']['main_y'][:, :num_players], platform_height_f32=fod_height, platform_height_valid_u8=fod_valid, platform_air_land_stick_y_threshold=platform_air_land_stick_y_threshold, floor_skip_frames=floor_skip_frames, data_root=data_root)
        samples['seed_t']['floor_skip_segment_id_u16'][:, :num_players] = fod_floor_skip
        samples['seed_t']['floor_skip_segment_valid_u8'][:, :num_players] = (fod_floor_skip != np.uint16(65535)).astype(np.uint8)
    counter_post = derive_instance_id_counter(fighter_instance_id_u16_2d=post_instance_id[:, :num_players], item_instance_id_u16_2d=items_fixed['instance_id'])
    samples['seed_t']['instance_id_counter'] = counter_post[:-1]
    item_spawn_counter_post = derive_item_spawn_id_counter(item_exists_u8_2d=items_fixed['exists'], item_spawn_id_u32_2d=items_fixed['spawn_id'])
    samples['seed_t']['item_spawn_id_counter'] = item_spawn_counter_post[:-1]
    entry_changed = post_action_id[1:, :] != post_action_id[:-1, :]
    instance_changed = post_instance_id[1:, :] != post_instance_id[:-1, :]
    has_ref_instance = post_instance_id[1:, :] != np.uint16(0)
    same_frame_fighter_entries_all = entry_changed & instance_changed & has_ref_instance
    multi_entry_frame = np.sum(same_frame_fighter_entries_all[:, :num_players], axis=1) >= 2
    hidden_same_frame_entry_order_owner = same_frame_fighter_entries_all & (multi_entry_frame[:, None] | (post_instance_id[1:, :] != counter_post[:-1, None]))
    hidden_same_frame_entry_order_owner &= post_action_id[1:, :] != np.uint16(act_attack_lw3)
    specialn_loop_restart_owner = instance_changed & has_ref_instance & (post_action_id[1:, :] == post_action_id[:-1, :]) & np.isin(post_action_id[1:, :], np.array([act_fx_special_n_loop, act_fx_special_air_n_loop], dtype=np.uint16)) & (post_action_frame[1:, :] == np.int16(0))
    specialn_blaster_loop_latch_owner = specialn_loop_restart_owner & np.isin(post_char_id_u8[1:, :], np.array([1, 22], dtype=np.uint8))
    specialn_cmd0_edge_latch_owner = np.zeros((n_frames - 1, 4), dtype=bool)
    seed_char = samples['seed_t']['char_id']
    seed_msid = samples['seed_t']['animation_index'].astype(np.uint32) & np.uint32(65535)
    seed_af = samples['seed_t']['action_frame'].astype(np.int16)
    seed_b_timer = samples['seed_t']['x67D'].astype(np.uint8)
    b_press_af = seed_af.astype(np.int32) - seed_b_timer.astype(np.int32)
    for (char_id, msid), (cmd0_on, cmd0_off_tail) in specialn_loop_cmd0_windows_by_char_msid.items():
        specialn_cmd0_edge_latch_owner |= (seed_char == np.uint8(char_id)) & (seed_msid == np.uint32(msid)) & (seed_b_timer != np.uint8(255)) & (b_press_af >= int(cmd0_on)) & (b_press_af < int(cmd0_off_tail))
    act_dead_down = np.uint16(0)
    act_dead_left = np.uint16(1)
    act_dead_right = np.uint16(2)
    act_dead_up_star = np.uint16(4)
    act_rebirth = np.uint16(12)
    act_rebirth_wait = np.uint16(13)
    act_fall = np.uint16(29)
    match_flow_instance_owner = same_frame_fighter_entries_all & (np.isin(post_action_id[1:, :], np.array([act_dead_down, act_dead_left, act_dead_right, act_rebirth, act_rebirth_wait, act_fall], dtype=np.uint16)) | np.isin(post_action_id[:-1, :], np.array([act_rebirth, act_rebirth_wait, act_dead_up_star], dtype=np.uint16))) & (multi_entry_frame[:, None] | (post_instance_id[1:, :] != counter_post[:-1, None]))
    guard_collision_instance_owner = same_frame_fighter_entries_all & (np.isin(post_action_id[1:, :], np.array([act_guard_on, act_guard, act_guard_off, act_guard_set_off, act_guard_reflect], dtype=np.uint16)) | np.isin(post_action_id[:-1, :], np.array([act_guard_on, act_guard, act_guard_off, act_guard_set_off, act_guard_reflect], dtype=np.uint16))) & (multi_entry_frame[:, None] | (post_instance_id[1:, :] != counter_post[:-1, None]))
    motion_entry_iid_override = np.zeros((n_frames - 1, 4), dtype=np.uint16)
    motion_entry_override_mask = hidden_same_frame_entry_order_owner
    motion_entry_override_mask |= specialn_loop_restart_owner
    motion_entry_override_mask |= match_flow_instance_owner
    motion_entry_override_mask |= guard_collision_instance_owner
    motion_entry_iid_override[motion_entry_override_mask] = post_instance_id[1:, :][motion_entry_override_mask]
    samples['seed_t']['motion_entry_instance_id_override_u16'][:, :] = motion_entry_iid_override
    samples['seed_t']['specialn_blaster_loop_requested'][:, :] = (specialn_blaster_loop_latch_owner | specialn_cmd0_edge_latch_owner).astype(np.uint8)
    samples['seed_t']['sheik_vanish_travel_timer_u8'][:, :] = _derive_sheik_vanish_travel_timer(char_id_u8=samples['seed_t']['char_id'], action_id_u16=samples['seed_t']['action_id'], sheik_internal_id=sheik_char_id, travel_frames=sheik_vanish_travel_frames)
    sheik_vanish_floor_skip = _derive_sheik_vanish_floor_skip_segments(stage_id=int(stage_id), action_id_u16=samples['seed_t']['action_id'][:, :num_players], char_id_u8=samples['seed_t']['char_id'][:, :num_players], on_ground_u8=samples['seed_t']['on_ground'][:, :num_players], ground_id_u16=samples['seed_t']['ground_id'][:, :num_players], vanish_travel_timer_u8=samples['seed_t']['sheik_vanish_travel_timer_u8'][:, :num_players], pos_x_f32=samples['seed_t']['pos_x'][:, :num_players], pos_y_f32=samples['seed_t']['pos_y'][:, :num_players], sheik_internal_id=sheik_char_id, travel_frames=sheik_vanish_travel_frames, ground_contact_min_frames=sheik_vanish_ground_contact_min_frames, data_root=data_root)
    current_floor_skip = samples['seed_t']['floor_skip_segment_id_u16'][:, :num_players]
    current_floor_skip_valid = current_floor_skip != np.uint16(65535)
    sheik_floor_skip_valid = sheik_vanish_floor_skip != np.uint16(65535)
    samples['seed_t']['floor_skip_segment_id_u16'][:, :num_players] = np.where(current_floor_skip_valid, current_floor_skip, sheik_vanish_floor_skip).astype(np.uint16)
    samples['seed_t']['floor_skip_segment_valid_u8'][:, :num_players] = (current_floor_skip_valid | sheik_floor_skip_valid).astype(np.uint8)
    sheik_chain_article_present = np.zeros_like(samples['seed_t']['char_id'], dtype=np.uint8)
    if sheik_char_id is not None and int(sheik_char_id) >= 0:
        import msl_binding
        sheik_chain_itkind = int(msl_binding.item_article_params(int(sheik_char_id))['sheik_chain_itkind'])
        seed_items = samples['seed_t']['items']
        is_chain_item = (seed_items['exists'] != 0) & (seed_items['type'] == np.uint16(sheik_chain_itkind))
        chain_owner = seed_items['owner']
        for p in range(int(num_players)):
            sheik_chain_article_present[:, p] = np.any(is_chain_item & (chain_owner == np.int8(p)), axis=1).astype(np.uint8)
    samples['seed_t']['sheik_needle_count_u8'][:, :], samples['seed_t']['sheik_needle_specialn_timer_u8'][:, :] = _derive_sheik_needle_seed_lanes(char_id_u8=samples['seed_t']['char_id'], action_id_u16=samples['seed_t']['action_id'], action_frame_i16=samples['seed_t']['action_frame'], chain_article_present_u8=sheik_chain_article_present, sheik_internal_id=sheik_char_id)
    samples['seed_t']['sheik_chain_x0_u8'][:, :], samples['seed_t']['sheik_chain_release_latch_u8'][:, :] = _derive_sheik_chain_seed_lanes(char_id_u8=samples['seed_t']['char_id'], action_id_u16=samples['seed_t']['action_id'], buttons_u16=samples['input_t']['p']['buttons'], hitlag_u16=samples['seed_t']['hitlag'], sheik_internal_id=sheik_char_id, b_mask=button_mask_b, release_min_frames=sheik_chain_release_min_frames)
    if sheik_char_id >= 0:
        sheik_needle_shoot_rng_owner_by_player = (samples['seed_t']['char_id'] == np.uint8(sheik_char_id)) & np.isin(samples['seed_t']['action_id'], np.array([344, 348], dtype=np.uint16)) & np.isin(samples['seed_t']['action_frame'], np.array([2, 5, 8, 11, 14, 17], dtype=np.int16)) & (samples['seed_t']['sheik_needle_count_u8'] != 0)
        sheik_needle_shoot_rng_owner = np.any(sheik_needle_shoot_rng_owner_by_player, axis=1)
        if np.any(sheik_needle_shoot_rng_owner):
            samples['seed_t']['frame_pre_random_seed'][sheik_needle_shoot_rng_owner] = frame_pre_random_seed[1:][sheik_needle_shoot_rng_owner]
    samples['seed_t']['match_flow_pending_rebirth_char_id'][:, :] = _derive_match_flow_pending_rebirth_char_id(post_action_id_u16=post_action_id_u16, post_char_id_u8=post_char_id_u8, post_stocks_u8=post_stocks_u8_all, match_flow_timer_u8=match_flow_timer_u8_all, static_char_id_u8=static_char_id_u8, team_id_u8=team_id_u8, is_teams=bool(is_teams), num_players=int(num_players))[:-1, :]
    samples['seed_t']['match_flow_pending_rebirth_state_flags_2218'][:, :] = _derive_match_flow_pending_rebirth_state_flags_2218(post_action_id_u16=post_action_id_u16, post_char_id_u8=post_char_id_u8, post_stocks_u8=post_stocks_u8_all, post_state_flags_u8=post_state_flags_u8, match_flow_timer_u8=match_flow_timer_u8_all, static_char_id_u8=static_char_id_u8, team_id_u8=team_id_u8, is_teams=bool(is_teams), num_players=int(num_players))[:-1, :]
    if int(num_players) == 2:
        grab_owner = derive_grab_owner_port_2p(action_id_u16_2p=post_action_id[:, :2])
        samples['seed_t']['grab_owner_port'][:, :2] = grab_owner[:-1, :]
    elif int(num_players) > 2:
        grab_owner = derive_grab_owner_port(action_id_u16=post_action_id[:, :int(num_players)], num_players=int(num_players))
        samples['seed_t']['grab_owner_port'][:, :int(num_players)] = grab_owner[:-1, :]
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native validation item buffer derivation is required; run `make build`') from exc
    hitbox_mask_lut = _u8_lut_from_pairs(tuple(sorted(((int(k), int(v)) for k, v in throw_laser_hitbox_masks.items()))))
    msl_binding.validation_derive_throw_laser_item_hitlist_buffers(_structured_rows_as_writable_bytes(samples['seed_t']), hitbox_mask_lut, int(num_players))
    laser_lut = _u8_lut_from_items(tuple((int(k) for k in laser_item_types)))
    shield_bounce_lut = _u8_lut_from_items(tuple((int(k) for k in shield_bounce_item_types)))
    msl_binding.validation_derive_item_hidden_callback_buffers(_structured_rows_as_writable_bytes(samples['seed_t']), _structured_rows_as_bytes(samples['ref_t1']), laser_lut, shield_bounce_lut, int(num_players))
    pre_stick_x_unit_2d = np.zeros((n_frames, 4), dtype=np.float32)
    pre_stick_y_unit_2d = np.zeros((n_frames, 4), dtype=np.float32)
    grab_mash_x_sign_post = np.zeros((n_frames, 4), dtype=np.int8)
    grab_mash_y_sign_post = np.zeros((n_frames, 4), dtype=np.int8)
    for slot in range(num_players):
        _, _, stick_x, stick_y = process_stick_i8_units(pre_main_x_2d[:, slot], pre_main_y_2d[:, slot], ucf_enabled=ucf_enabled, ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled, deadzone_x=float(lstick_deadzone_x), deadzone_y=float(lstick_deadzone_y))
        pre_stick_x_unit_2d[:, slot] = stick_x
        pre_stick_y_unit_2d[:, slot] = stick_y
        mash_x, mash_y = derive_grab_mash_stick_sign_post(stick_x_unit=stick_x, stick_y_unit=stick_y, action_id_u16=post_action_id[:, slot], action_frame_i16=post_action_frame[:, slot], grab_owner_port_u8=grab_owner[:, slot], grab_mash_stick_threshold=grab_mash_stick_threshold)
        grab_mash_x_sign_post[:, slot] = mash_x
        grab_mash_y_sign_post[:, slot] = mash_y
        samples['seed_t']['grab_mash_stick_x_sign'][:, slot] = mash_x[:-1]
        samples['seed_t']['grab_mash_stick_y_sign'][:, slot] = mash_y[:-1]
    capture_mash_buttons_pressed = derive_capture_mash_buttons_pressed(buttons_u16_2d=pre_buttons, l_trigger_u8_2d=pre_l, r_trigger_u8_2d=pre_r, trigger_deadzone=float(common['trigger_deadzone']), button_mask_a=button_mask_a, button_mask_z=button_mask_z, button_mask_lr=button_mask_lr)
    phantom_damage_pending, phantom_damage_timer, phantom_damage_source_port = _derive_phantom_damage_pending_seed_lanes(percent_f32=post_percent_all, hitlag_u16=post_hitlag, action_id_u16=post_action_id, instance_hit_by_u16=post_instance_hit_by, instance_id_u16=post_instance_id, num_players=num_players)
    samples['seed_t']['phantom_damage_pending_x1898'] = phantom_damage_pending[:-1]
    samples['seed_t']['phantom_damage_timer_x189c'] = phantom_damage_timer[:-1]
    samples['seed_t']['phantom_damage_source_port'] = phantom_damage_source_port[:-1]
    for slot, port_1based in enumerate(src_ports[:int(num_players)]):
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0, handicap=9))
        capture_grab_timer, capture_wait_counter, capture_wait_anim_timer, capture_wait_jump_latch, capture_breakout_pending = derive_capture_grab_hidden_post(action_id_u16=post_action_id[:, slot], action_frame_i16=post_action_frame[:, slot], grab_owner_port_u8=grab_owner[:, slot], percent_f32=post_percent_all[:, slot], buttons_pressed_u16=capture_mash_buttons_pressed[:, slot], stick_x_unit=pre_stick_x_unit_2d[:, slot], stick_y_unit=pre_stick_y_unit_2d[:, slot], frame_speed_mul_f32=frame_speed_mul_all[:, slot], grab_mash_stick_x_sign_post=grab_mash_x_sign_post[:, slot], grab_mash_stick_y_sign_post=grab_mash_y_sign_post[:, slot], slot_index=slot, handicap=st.handicap, capture_grab_timer_base=float(common['capture_grab_timer_base']), capture_grab_timer_handicap_mul=float(common['capture_grab_timer_handicap_mul']), capture_grab_timer_handicap_base=float(common['capture_grab_timer_handicap_base']), capture_grab_timer_slot_mul=float(common['capture_grab_timer_slot_mul']), capture_grab_timer_slot_base=float(common['capture_grab_timer_slot_base']), capture_grab_timer_percent_mul=float(common['capture_grab_timer_percent_mul']), capture_wait_grab_timer_decrement=float(common['capture_wait_grab_timer_decrement']), capture_wait_grab_mash_damage=float(common['capture_wait_grab_mash_damage']), capture_wait_anim_rate_hold_frames=float(common['capture_wait_anim_rate_hold_frames']), capture_wait_jump_latch_window_frames=float(common['capture_wait_jump_latch_window_frames']), grab_mash_stick_threshold=grab_mash_stick_threshold)
        samples['seed_t']['capture_grab_timer_f32'][:, slot] = capture_grab_timer[:-1]
        samples['seed_t']['capture_wait_counter_f32'][:, slot] = capture_wait_counter[:-1]
        samples['seed_t']['capture_wait_anim_rate_timer_f32'][:, slot] = capture_wait_anim_timer[:-1]
        samples['seed_t']['capture_wait_jump_latch_u8'][:, slot] = capture_wait_jump_latch[:-1]
        samples['seed_t']['capture_breakout_pending_u8'][:, slot] = capture_breakout_pending[:-1]
    if n_frames >= 2:
        post_facing[:-1, :] = samples['seed_t']['facing'][:, :]
        post_facing[-1, :] = post_facing[-2, :]
        post_guard_tilt_x8[:-1, :] = samples['seed_t']['guard_tilt_x8'][:, :]
        post_guard_tilt_x8[-1, :] = post_guard_tilt_x8[-2, :]
        post_guard_tilt_x4[:-1, :] = samples['seed_t']['guard_tilt_x4'][:, :]
        post_guard_tilt_x4[-1, :] = post_guard_tilt_x4[-2, :]
    hitlist_cd, hitlist_iid, hitlist_hb_valid, hitlist_hb_cd, hitlist_hb_iid, shield_contact_hb_kind = derive_combat_hitlist_seed_fields(num_players=num_players, is_teams=bool(is_teams), team_id=post_team_id, char_id=post_char_id, action_id=post_action_id, action_frame=post_action_frame, animation_index=post_animation_index, facing=post_facing, on_ground=post_on_ground, pos_x=post_pos_x, pos_y=post_pos_y, fighter_scale_y=post_scale_y, guard_tilt_x8=post_guard_tilt_x8, guard_tilt_x4=post_guard_tilt_x4, stocks=post_stocks, percent=post_percent_all, shield_hp=post_shield_hp, hurtbox_state=post_hurtbox_state, hitlag=post_hitlag, last_hit_by=post_last_hit_by, source_port0=post_source_port0, instance_hit_by=post_instance_hit_by, instance_id=post_instance_id, input_buttons=pre_buttons, input_l=pre_l, input_r=pre_r, turn_has_turned=post_turn_has_turned_u8, anim_frame_f32=post_anim_frame, frame_speed_mul_f32=frame_speed_mul_all, specialhi_rotate_model_f32=specialhi_rotate_model_all, specialhi_rotate_model_valid_u8=specialhi_rotate_model_valid_all, include_per_hitbox=True, include_replay_only_shield_admission=True, include_replay_only_body_admission=True, data_root='data')
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.trim_stale_hitlist_seed_bridge is required; run `make build`') from exc
    msl_binding.trim_stale_hitlist_seed_bridge(hitlist_cd, hitlist_iid, hitlist_hb_valid, hitlist_hb_cd, hitlist_hb_iid, _ascontiguousarray(post_instance_id, dtype=np.uint16), _ascontiguousarray(post_instance_hit_by, dtype=np.uint16), _ascontiguousarray(post_last_hit_by, dtype=np.uint8), _ascontiguousarray(post_hitlag, dtype=np.uint16), _ascontiguousarray(post_hitstun, dtype=np.uint16), _ascontiguousarray(post_action_id, dtype=np.uint16), _ascontiguousarray(post_landing_fallspecial_allow_interrupt, dtype=np.uint8), int(num_players), int(act_attack_11), int(act_attack_lw4), int(act_damage_fly_top), int(act_landing_fall_special), int(act_guard_on), int(act_guard), int(act_guard_set_off), int(act_guard_reflect), int(act_guard_off))
    guard_family_actions = np.array([act_guard_on, act_guard, act_guard_off, act_guard_set_off, act_guard_reflect], dtype=np.uint16)
    attack_contact_actions = np.arange(int(act_attack_11), int(act_attack_air_lw) + 1, dtype=np.uint16)
    same_frame_special_contact_entry_actions = np.array([act_fx_special_lw_start, act_fx_special_lw_loop, act_fx_special_lw_hit, act_fx_special_lw_turn, act_fx_special_air_lw_start, act_fx_special_air_lw_loop, act_fx_special_air_lw_hit, act_fx_special_air_lw_turn], dtype=np.uint16)
    same_frame_contact_entry_actions = np.concatenate((attack_contact_actions, same_frame_special_contact_entry_actions))
    shield_hit_int_damage = np.zeros((n_frames, samples['seed_t']['combat_shield_hit_int_damage'].shape[1]), dtype=np.uint8)
    shield_damage_taken = np.zeros((n_frames, samples['seed_t']['combat_shield_damage_taken'].shape[1]), dtype=np.uint8)
    guard_lut = _u8_lut_from_items(tuple((int(k) for k in guard_family_actions)))
    attack_lut = _u8_lut_from_items(tuple((int(k) for k in attack_contact_actions)))
    same_frame_lut = _u8_lut_from_items(tuple((int(k) for k in same_frame_contact_entry_actions)))
    same_frame_special_lut = _u8_lut_from_items(tuple((int(k) for k in same_frame_special_contact_entry_actions)))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_shield_contact_seed_bridge is required; run `make build`') from exc
    shield_hit_int_damage, shield_damage_taken = msl_binding.derive_shield_contact_seed_bridge(shield_contact_hb_kind, hitlist_hb_valid, hitlist_hb_cd, hitlist_hb_iid, _ascontiguousarray(post_action_id, dtype=np.uint16), _ascontiguousarray(post_hitlag, dtype=np.uint16), _ascontiguousarray(post_instance_id, dtype=np.uint16), _ascontiguousarray(post_shield_f32_all, dtype=np.float32), _ascontiguousarray(lightshield_amount_all, dtype=np.float32), _ascontiguousarray(post_animation_index_u32_all, dtype=np.uint32), _ascontiguousarray(post_state_age_all, dtype=np.int16), _ascontiguousarray(post_char_id_u8, dtype=np.uint8), _ascontiguousarray(hist.attack_id, dtype=np.uint16), _ascontiguousarray(hist.stale_queue_index, dtype=np.uint8), _ascontiguousarray(hist.stale_move_id, dtype=np.uint16), manifest_tables.active_shield_hit_lut, _ascontiguousarray(stale_weights, dtype=np.float32), guard_lut, attack_lut, same_frame_lut, same_frame_special_lut, int(num_players), int(act_guard_set_off), float(common['hitlag_dmg_mul']), float(common['hitlag_base']), float(shield_hit_mul), float(shield_hit_base), float(shield_hit_ls_min), float(shield_hit_ls_max), float(shield_hold_drain_mul), float(shield_hold_drain_base), float(shield_hold_drain_max))
    samples['seed_t']['combat_hitlist_cd'] = hitlist_cd[:-1]
    samples['seed_t']['combat_hitlist_victim_iid'] = hitlist_iid[:-1]
    samples['seed_t']['combat_hitlist_hb_valid'] = hitlist_hb_valid[:-1]
    samples['seed_t']['combat_hitlist_hb_cd'] = hitlist_hb_cd[:-1]
    samples['seed_t']['combat_hitlist_hb_victim_iid'] = hitlist_hb_iid[:-1]
    samples['seed_t']['combat_shield_contact_hb_kind'] = shield_contact_hb_kind[:-1]
    samples['seed_t']['combat_shield_hit_int_damage'] = shield_hit_int_damage[:-1]
    samples['seed_t']['combat_shield_damage_taken'] = shield_damage_taken[:-1]
    rebound_ground_accel_2 = np.zeros((n_frames, samples['seed_t']['rebound_ground_accel_2_f32'].shape[1]), dtype=np.float32)
    rebound_anim_rate = np.zeros((n_frames, samples['seed_t']['rebound_anim_rate_f32'].shape[1]), dtype=np.float32)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_rebound_seed_lanes is required; run `make build`') from exc
    rebound_ground_accel_2, rebound_anim_rate = msl_binding.derive_rebound_seed_lanes(_ascontiguousarray(post_action_id, dtype=np.uint16), _ascontiguousarray(post_hitlag, dtype=np.uint16), _ascontiguousarray(post_on_ground, dtype=np.uint8), _ascontiguousarray(post_char_id, dtype=np.uint8), _ascontiguousarray(samples['seed_t']['speed_ground_x_self'], dtype=np.float32), _ascontiguousarray(samples['ref_t1']['speed_ground_x_self'], dtype=np.float32), _ascontiguousarray(samples['seed_t']['frame_speed_mul_f32'], dtype=np.float32), manifest_tables.rebound_numerator_lut, int(num_players), int(act_rebound_stop), int(act_rebound), float(common.get('rebound_ground_x0_mul', 0.0)), float(common.get('rebound_ground_x0_base', 0.0)))
    samples['seed_t']['rebound_ground_accel_2_f32'] = rebound_ground_accel_2[:-1]
    samples['seed_t']['rebound_anim_rate_f32'] = rebound_anim_rate[:-1]
    hitbox_prev_valid, hitbox_prev_x, hitbox_prev_y, hitbox_prev_z = derive_hitbox_prev_center_seed_fields(num_players=num_players, char_id=post_char_id, action_id=post_action_id, animation_index=post_animation_index, action_frame=post_action_frame, anim_frame_f32=post_anim_frame, pos_x=post_pos_x, pos_y=post_pos_y, pos_z=hidden_pos_z, facing=post_facing, fighter_scale_y=post_scale_y, specialhi_rotate_model_f32=specialhi_rotate_model_all, specialhi_rotate_model_valid_u8=specialhi_rotate_model_valid_all, data_root='data')
    samples['seed_t']['combat_hitbox_prev_valid'] = hitbox_prev_valid[:-1]
    samples['seed_t']['combat_hitbox_prev_x'] = hitbox_prev_x[:-1]
    samples['seed_t']['combat_hitbox_prev_y'] = hitbox_prev_y[:-1]
    samples['seed_t']['combat_hitbox_prev_z'] = hitbox_prev_z[:-1]
    from tools.slippi.combo_history import derive_combo_push_timer_seed, derive_combo_seed_fields
    combo_victim_port, combo_victim_iid, combo_timer = derive_combo_seed_fields(num_players=num_players, src_ports=list(src_ports), hitlag=post_hitlag, state_flags=post_state_flags, instance_id=post_instance_id, last_hit_by=post_last_hit_by, instance_hit_by=post_instance_hit_by, data_root='data')
    samples['seed_t']['combo_victim_port'][:, :num_players] = combo_victim_port[:-1, :num_players]
    samples['seed_t']['combo_victim_instance_id'][:, :num_players] = combo_victim_iid[:-1, :num_players]
    samples['seed_t']['combo_timer_x2098'][:, :num_players] = combo_timer[:-1, :num_players]
    combo_push_timer = derive_combo_push_timer_seed(combo_count=post_combo_count_u8_all, last_attack_landed=post_last_attack_landed_u8_all, combo_victim_port=combo_victim_port, data_root='data')
    samples['seed_t']['combo_push_timer_x2092'][:, :num_players] = combo_push_timer[:-1, :num_players]
    dream_wind_dir, dream_wind_valid, dream_wind_timer = _derive_dream_whispy_wind_seed_lanes(samples, stage_id=int(stage_id), num_players=int(num_players))
    samples['seed_t']['stage_dream_whispy_wind_dir_u8'] = dream_wind_dir
    samples['seed_t']['stage_dream_whispy_wind_valid_u8'] = dream_wind_valid
    samples['seed_t']['stage_dream_whispy_wind_timer_u16'] = dream_wind_timer
    return samples.as_buffers(num_players=int(num_players))
