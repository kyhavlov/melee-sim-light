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

def _derive_throw_pulse_seed_lanes(*, seed_action_id_u16: np.ndarray, seed_char_id_u8: np.ndarray, seed_anim_frame_f32: np.ndarray, seed_frame_speed_mul_f32: np.ndarray, seed_hitstun_u16: np.ndarray, seed_last_attack_landed_u8: np.ndarray, seed_last_hit_by_u8: np.ndarray, seed_items: np.ndarray, num_players: int, pulse_frames_by_char_action: dict[tuple[int, int], tuple[int, ...]], cmd1_start_by_char_action: dict[tuple[int, int], int], shot_itkind_by_char: dict[int, int], act_throw_b: int, act_throw_hi: int, act_damage_fly_top: int, falco_char_id: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive throw pulse seed lanes strictly from seed-visible replay lanes.

    Decomp ownership:
    - Throw-side shots come from one-shot throw_flags_b0 pulses consumed in ftFx_Throw_Anim.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}

    Seed policy:
    - Mark rows where one-step throw pulse reconstruction should be suppressed:
      - script timing stale windows from throw move events (ThrowB/ThrowHi pulse crossing windows),
      - and decomp-owned ongoing-damage throw-laser contexts previously gated in runtime C.
    - Record prior-step pulse crossing frame for future throw command cursor ownership:
      - 0 means no crossing in (t-1 -> t),
      - N is the crossed pulse frame from extracted throw move events.
    """
    max_action = max((int(action) for _, action in pulse_frames_by_char_action.keys()), default=0)
    max_pulses = max((len(v) for v in pulse_frames_by_char_action.values()), default=0)
    if max_pulses <= 0:
        shape = (int(seed_action_id_u16.shape[0]), 4)
        return (np.zeros(shape, dtype=np.uint8), np.zeros(shape, dtype=np.uint8), np.zeros(shape, dtype=np.uint8))
    pulse_lut = np.zeros((256, max_action + 1, max_pulses), dtype=np.int16)
    pulse_count_lut = np.zeros((256, max_action + 1), dtype=np.uint8)
    cmd1_lut = np.full((256, max_action + 1), -1, dtype=np.int16)
    shot_lut = np.zeros(256, dtype=np.uint16)
    for (cid, action), pulses in pulse_frames_by_char_action.items():
        cid_i = int(cid) & 255
        action_i = int(action)
        pulse_count_lut[cid_i, action_i] = np.uint8(len(pulses))
        for k, pulse in enumerate(pulses):
            pulse_lut[cid_i, action_i, k] = np.int16(int(pulse))
    for (cid, action), frame in cmd1_start_by_char_action.items():
        cmd1_lut[int(cid) & 255, int(action)] = np.int16(int(frame))
    for cid, kind in shot_itkind_by_char.items():
        shot_lut[int(cid) & 255] = np.uint16(int(kind))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_throw_pulse_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_throw_pulse_seed_lanes(_ascontiguousarray(seed_action_id_u16, dtype=np.uint16), _ascontiguousarray(seed_char_id_u8, dtype=np.uint8), _ascontiguousarray(seed_anim_frame_f32, dtype=np.float32), _ascontiguousarray(seed_frame_speed_mul_f32, dtype=np.float32), _ascontiguousarray(seed_hitstun_u16, dtype=np.uint16), _ascontiguousarray(seed_last_attack_landed_u8, dtype=np.uint8), pulse_lut, pulse_count_lut, cmd1_lut, shot_lut, int(num_players), int(act_throw_b), int(act_throw_hi), int(falco_char_id))

def _derive_throw_laser_item_hitlist_seed_lanes(*, seed_action_id_u16: np.ndarray, seed_grab_owner_port_u8: np.ndarray, seed_instance_hit_by_u16: np.ndarray, seed_instance_id_u16: np.ndarray, seed_items: np.ndarray, num_players: int, throw_laser_hitbox_masks: dict[int, np.uint8]) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive a compact per-item victims_1 seed lane for throw-side laser articles.

    Decomp ownership:
    - Item BODY collision inserts fighter victims into HitCapsule.victims_1 through
      it_8026FAC4 / it_8026FA2C -> lbColl_80008688 before it_80272460 damage callbacks.
    - Throw-side laser articles are spawned by ftFx_Throw_Anim through it_8029C6CC, but their
      BODY rehit/carry decision is item HitCapsule state, not command timing alone.

    Producer policy:
    - Keep the lane prefix-causal and narrow: only state1 throw laser items whose seeded xDA8
      identity (`item.instance_id`) matches an attached grabbed/thrown victim's
      `instance_hit_by`.
    - Dolphin v10 dumps show the authoritative state is per item HitCapsule, not per item: Fox
      ThrowLw attached rows populate hitboxes 0/1, while Falco rows can populate 2/3 while 0/1
      remain BODY-eligible. The seed lane therefore carries a compact hitbox mask instead of an
      item-wide latch; Falco production remains disabled until terminal clear/callback phase is
      explicitly represented.
    - This covers replay-real attached-pulse carry rows without broadening to ordinary ThrowHi
      hitstun rows such as BHH:937 where the dump showed no relevant item hitlist entry.

    refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}
    refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    refs/Ishiiruka branch engine-dump-v12-probes (EngineDumpWriter item hitlist lanes)
    """
    n_samples = int(seed_action_id_u16.shape[0])
    if n_samples == 0:
        shape = (0, 15)
        return (np.full(shape, 255, dtype=np.uint8), np.zeros(shape, dtype=np.uint8), np.zeros(shape, dtype=np.uint8), np.zeros(shape, dtype=np.uint16))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_throw_laser_item_hitlist_seed_lanes is required; run `make build`') from exc
    hitbox_mask_lut = _u8_lut_from_pairs(tuple(sorted(((int(k), int(v)) for k, v in throw_laser_hitbox_masks.items()))))
    return msl_binding.derive_throw_laser_item_hitlist_seed_lanes(_ascontiguousarray(seed_action_id_u16, dtype=np.uint16), _ascontiguousarray(seed_grab_owner_port_u8, dtype=np.uint8), _ascontiguousarray(seed_instance_hit_by_u16, dtype=np.uint16), _ascontiguousarray(seed_instance_id_u16, dtype=np.uint16), _ascontiguousarray(seed_items['exists'], dtype=np.uint8), _ascontiguousarray(seed_items['state'], dtype=np.uint8), _ascontiguousarray(seed_items['type'], dtype=np.uint16), _ascontiguousarray(seed_items['owner'], dtype=np.int8), _ascontiguousarray(seed_items['instance_id'], dtype=np.uint16), hitbox_mask_lut, int(num_players))

def _fill_items_fixed(frames: pa.StructArray, n_frames: int, *, src_ports: list[int]) -> np.ndarray:
    """
    Convert Slippi frame items (list<struct<...>>) into a fixed-length [n_frames, 15]
    array matching the validation ITEM dtype, with a stable ordering.

    Ordering: sort by (instance_id, spawn_id/id, type).

    Owner lane policy:
    - Slippi item.owner is emitted in raw 0-based console-port space (P1->0 .. P4->3).
    - Validation buffers store fighters/items in the selected local slot order (`src_ports`), so item
      owner must be remapped into that local slot domain at preprocessing time.
    - If the raw owner does not correspond to one of the selected ports, store -1.
    """
    out = np.zeros((n_frames, 15), dtype=SEED_DTYPE['items'].base)
    out['owner'] = np.int8(-1)
    if 'attack_id' in out.dtype.names:
        out['attack_id'] = np.uint16(1)
    if 'attack_instance' in out.dtype.names:
        out['attack_instance'] = np.uint16(0)
    if 'item' not in {f.name for f in frames.type}:
        return out
    items_list = frames.field('item')
    values = items_list.values
    velocity = values.field('velocity')
    position = values.field('position')
    misc = values.field('misc')
    owner_map = np.full(4, -1, dtype=np.int8)
    for slot, port in enumerate(src_ports):
        raw_port = int(port) - 1
        if 0 <= raw_port < 4:
            owner_map[raw_port] = np.int8(slot)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.fill_items_fixed is required; run `make build`') from exc
    msl_binding.fill_items_fixed(_ascontiguousarray(_to_numpy(items_list.offsets), dtype=np.int32), _ascontiguousarray(_to_numpy(values.field('type')), dtype=np.uint16), _ascontiguousarray(_to_numpy(values.field('state')), dtype=np.uint8), _ascontiguousarray(_to_numpy(values.field('direction')), dtype=np.float32), _ascontiguousarray(_to_numpy(velocity.field('x')), dtype=np.float32), _ascontiguousarray(_to_numpy(velocity.field('y')), dtype=np.float32), _ascontiguousarray(_to_numpy(position.field('x')), dtype=np.float32), _ascontiguousarray(_to_numpy(position.field('y')), dtype=np.float32), _ascontiguousarray(_to_numpy(values.field('damage')), dtype=np.uint16), _ascontiguousarray(_to_numpy(values.field('timer')), dtype=np.float32), _ascontiguousarray(_to_numpy(values.field('id')), dtype=np.uint32), _ascontiguousarray(_to_numpy(misc.field('0')), dtype=np.uint8), _ascontiguousarray(_to_numpy(misc.field('1')), dtype=np.uint8), _ascontiguousarray(_to_numpy(misc.field('2')), dtype=np.uint8), _ascontiguousarray(_to_numpy(misc.field('3')), dtype=np.uint8), _ascontiguousarray(_to_numpy(values.field('owner')), dtype=np.int8), _ascontiguousarray(_to_numpy(values.field('instance_id')), dtype=np.uint16), owner_map, out)
    return out

@functools.lru_cache(maxsize=1)
def _yoshi_shyguy_params():
    return yoshi_shyguy_metadata(Path('data'))

@functools.lru_cache(maxsize=1)
def _item_common_params() -> dict[str, float]:
    return _load_json_file(Path('data/items/item_common.json'))

@functools.lru_cache(maxsize=4)
def _laser_shot_item_kinds(path: Path) -> tuple[int, ...]:
    """Read supported Fox/Falco laser shot item kinds from generated MSLLASR1 data."""
    buf = _load_bytes_file(path)
    if len(buf) < 16 or buf[:8] != b'MSLLASR1':
        raise ValueError(f'{path}: invalid MSLLASR1 header')
    version = struct.unpack_from('<I', buf, 8)[0]
    count = struct.unpack_from('<H', buf, 12)[0]
    if version not in (4, 5, 6, 7):
        raise ValueError(f'{path}: unsupported MSLLASR1 version {version}')
    record_size = 226 if version >= 7 else 218 if version >= 6 else 254
    off = 16
    out: list[int] = []
    for _ in range(int(count)):
        if off + record_size > len(buf):
            raise ValueError(f'{path}: truncated MSLLASR1 record')
        out.append(int(struct.unpack_from('<H', buf, off + 2)[0]))
        off += record_size
    return tuple(out)

def _derive_yoshi_shyguy_native_lanes(items_fixed: np.ndarray, *, stage_id: int):
    """Derive Shy Guy seed lanes through the required native preprocessing path.

    This is an eval/preprocess hot path. Do not reintroduce Python per-frame/per-item loops here:
    the native wrapper owns the state maps, active AObj phase scan, stage timer/delay lanes, and
    item hitlag derivation.
    refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    refs/melee/src/melee/it/items/itheiho.c
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_yoshi_shyguy_seed_lanes is required for preprocessing; run `make build`') from exc
    params = _yoshi_shyguy_params()
    common = _item_common_params()
    return msl_binding.derive_yoshi_shyguy_seed_lanes(_ascontiguousarray(items_fixed['exists'], dtype=np.uint8), _ascontiguousarray(items_fixed['type'], dtype=np.uint16), _ascontiguousarray(items_fixed['owner'], dtype=np.int8), _ascontiguousarray(items_fixed['state'], dtype=np.uint8), _ascontiguousarray(items_fixed['spawn_id'], dtype=np.uint32), _ascontiguousarray(items_fixed['instance_id'], dtype=np.uint16), _ascontiguousarray(items_fixed['vel_x'], dtype=np.float32), _ascontiguousarray(items_fixed['vel_y'], dtype=np.float32), _ascontiguousarray(items_fixed['pos_x'], dtype=np.float32), _ascontiguousarray(items_fixed['pos_y'], dtype=np.float32), _ascontiguousarray(items_fixed['damage'], dtype=np.uint16), int(stage_id), int(params.stage_id), int(params.item_kind), int(params.timer_reset), int(params.spawn_delay_step), float(params.state4_speed_mul), np.asarray(params.vpos, dtype=np.float32), np.asarray(params.speed, dtype=np.float32), np.asarray(params.dyn_y_vel, dtype=np.float32), float(common['item_hitlag_damage_mul']), float(common['item_hitlag_base']))

def _derive_yoshi_shyguy_prev_vel_y(items_fixed: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    lanes = _derive_yoshi_shyguy_native_lanes(items_fixed, stage_id=int(_yoshi_shyguy_params().stage_id))
    return (lanes[0], lanes[1])

def _derive_yoshi_shyguy_dyn_y_phase(items_fixed: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    lanes = _derive_yoshi_shyguy_native_lanes(items_fixed, stage_id=int(_yoshi_shyguy_params().stage_id))
    return (lanes[2], lanes[3])

def _derive_yoshi_shyguy_seed_lanes(items_fixed: np.ndarray, *, stage_id: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    lanes = _derive_yoshi_shyguy_native_lanes(items_fixed, stage_id=int(stage_id))
    return (lanes[4], lanes[5], lanes[6], lanes[7], lanes[8], lanes[9], lanes[10], lanes[11], lanes[12])

def _structured_rows_as_bytes(rows: np.ndarray) -> np.ndarray:
    contiguous = _ascontiguousarray(rows)
    return contiguous.view(np.uint8).reshape(contiguous.shape[0], -1)

def _structured_rows_as_writable_bytes(rows: np.ndarray) -> np.ndarray:
    if not rows.flags.c_contiguous:
        raise ValueError('structured row array must be C-contiguous for writable native access')
    return rows.view(np.uint8).reshape(rows.shape[0], -1)

@functools.lru_cache(maxsize=1)
def _dream_whispy_params():
    return dream_whispy_metadata(Path('data'))

def _derive_dream_whispy_wind_seed_lanes(samples: np.ndarray, *, stage_id: int, num_players: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Derive Dream Land Whispy current-wind seed state through native simulation.

    Whispy's hidden `grOldPupupu` wind direction is not exported by Slippi. The native pass runs
    the current simulator without wind, compares the same-frame one-step fighter `pos_x` residual
    against the generated Dream Land wind speed, and promotes only the next row's current hidden
    direction. This is prefix-causal (`t-1 -> t`), not a replay-future position bridge.
    refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4}
    refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    """
    params = _dream_whispy_params()
    n = int(samples.shape[0])
    out_dir = np.zeros(n, dtype=np.uint8)
    valid = np.zeros(n, dtype=np.uint8)
    timer = np.zeros(n, dtype=np.uint16)
    if int(stage_id) != int(params.stage_id) or n == 0:
        return (out_dir, valid, timer)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_dream_whispy_wind_seed_lanes is required for preprocessing; run `make build`') from exc
    return msl_binding.validation_derive_dream_whispy_wind_seed_lanes(_structured_rows_as_bytes(samples['seed_t']), _structured_rows_as_bytes(samples['prev_input_t']), _structured_rows_as_bytes(samples['input_t']), _structured_rows_as_bytes(samples['ref_t1']), int(num_players), int(params.stage_id), float(params.wind_speed), 0.025)

def _materialize_illusion_seed_positions(items_fixed: np.ndarray, *, illusion_ghost_pos1_x: np.ndarray, illusion_ghost_pos1_y: np.ndarray, post_action_id_u16: np.ndarray, post_hitlag_u8: np.ndarray, post_instance_hit_by_u16: np.ndarray, num_players: int, illusion_item_kinds: tuple[int, ...]) -> np.ndarray:
    """Materialize Illusion/Phantasm seed positions causally from replay history.

    Rationale:
    - Decomp owner lane copies Illusion item position from fighter SpecialS ghost history
      (ftFx_SpecialS_CopyGhostPosIndexed(index=1)).
      refs/melee/src/melee/it/items/itfoxillusion.c::{
        itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CopyGhostPosIndexed
    - Slippi post-frame does not expose mv.fx.SpecialS.ghostEffectPos ring contents directly.

    Policy:
    - Materialize seed item positions from the same causal `ghostEffectPos[1]` lane promoted into
      seed_t for runtime ownership.
    """
    out = np.empty_like(items_fixed)
    np.copyto(out, items_fixed)
    n_frames = int(out.shape[0])
    if n_frames <= 1:
        return out
    update_mask, update_x, update_y = _derive_illusion_seed_position_updates(items_fixed=out, illusion_ghost_pos1_x=illusion_ghost_pos1_x, illusion_ghost_pos1_y=illusion_ghost_pos1_y, post_action_id_u16=post_action_id_u16, post_hitlag_u8=post_hitlag_u8, post_instance_hit_by_u16=post_instance_hit_by_u16, num_players=num_players, illusion_item_kinds=illusion_item_kinds)
    mask = update_mask != 0
    out['pos_x'][mask] = update_x[mask]
    out['pos_y'][mask] = update_y[mask]
    return out

def _derive_illusion_seed_position_updates(items_fixed: np.ndarray, *, illusion_ghost_pos1_x: np.ndarray, illusion_ghost_pos1_y: np.ndarray, post_action_id_u16: np.ndarray, post_hitlag_u8: np.ndarray, post_instance_hit_by_u16: np.ndarray, num_players: int, illusion_item_kinds: tuple[int, ...]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    n_frames = int(items_fixed.shape[0])
    if n_frames <= 1:
        shape = items_fixed['exists'].shape
        return (np.zeros(shape, dtype=np.uint8), np.zeros(shape, dtype=np.float32), np.zeros(shape, dtype=np.float32))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_illusion_seed_position_updates is required; run `make build`') from exc
    illusion_lut = _u8_lut_from_items(tuple((int(k) for k in illusion_item_kinds)))
    return msl_binding.derive_illusion_seed_position_updates(_ascontiguousarray(items_fixed['exists'], dtype=np.uint8), _ascontiguousarray(items_fixed['type'], dtype=np.uint16), _ascontiguousarray(items_fixed['owner'], dtype=np.int8), _ascontiguousarray(items_fixed['instance_id'], dtype=np.uint16), _ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_hitlag_u8, dtype=np.uint8), _ascontiguousarray(post_instance_hit_by_u16, dtype=np.uint16), _ascontiguousarray(illusion_ghost_pos1_x, dtype=np.float32), _ascontiguousarray(illusion_ghost_pos1_y, dtype=np.float32), illusion_lut, int(num_players))

def derive_illusion_ghost_pos012(*, post_action_id_u16: np.ndarray, post_action_frame_i16: np.ndarray, post_pos_x: np.ndarray, post_pos_y: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive post-frame `mv.fx.SpecialS.ghostEffectPos[0..2]` through native C.

    Decomp ownership:
    - ftFox_SpecialS_SetVars initializes ghostEffectPos[0..3] = cur_pos.
    - ftFox_SpecialS_SetPhys advances ghost3 <- ghost2 <- ghost1 <- ghost0 <- cur_pos.
    - Illusion item Phys consumes ghostEffectPos[1]; item collision preserves x58 from [2].
    refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c
    refs/melee/src/melee/it/items/itfoxillusion.c
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_illusion_ghost_pos012 is required; run `make build`') from exc
    return msl_binding.derive_illusion_ghost_pos012(_ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_action_frame_i16, dtype=np.int16), _ascontiguousarray(post_pos_x, dtype=np.float32), _ascontiguousarray(post_pos_y, dtype=np.float32))

def derive_illusion_ghost_pos01(*, post_action_id_u16: np.ndarray, post_action_frame_i16: np.ndarray, post_pos_x: np.ndarray, post_pos_y: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Helper for callers that only need ghostEffectPos[0..1]."""
    out0_x, out0_y, out1_x, out1_y, _out2_x, _out2_y = derive_illusion_ghost_pos012(post_action_id_u16=post_action_id_u16, post_action_frame_i16=post_action_frame_i16, post_pos_x=post_pos_x, post_pos_y=post_pos_y)
    return (out0_x, out0_y, out1_x, out1_y)

def _derive_item_attack_fields(items_fixed: np.ndarray, *, fighter_attack_id: np.ndarray, fighter_attack_instance: np.ndarray, num_players: int) -> None:
    """Derive per-item (attack_id, attack_instance) strictly causally (prefix-invariant).

    Decomp shape:
    - Items spawned from fighters copy fp->x2068_attackID / fp->x206C_attack_instance at spawn.
      refs/melee/src/melee/it/it_2725.c::it_8027B070
    - Fox/Falco laser shots can first appear in the serialized post-frame after the spawn callback
      has run and the owner has already left Blaster Loop. For shot item kinds from
      `data/items/lasers.bin`, the native derivation repairs only that first-visibility timing gap
      by using the previous post-frame owner identity when the same-frame owner identity is already
      FtMoveId_Default.
      refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
    - Reflected items can change owner/instance identity without despawning, but the item's staling
      identity remains spawn-latched for lasers in v1 (do not overwrite these fields on reflect).
      Slippi records item.instance_id from item->xDA8_short (SendItemInfo.s reads 0xDA8), and the
      reflect path updates xDA8_short from the reflecting fighter snapshot:
        refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464 (writes item reflect snapshot fields)
        refs/melee/src/melee/it/item.c::Item_80269F14 (applies reflect; updates owner + xDA8_short)
        refs/slippi-ssbm-asm/Recording/SendItemInfo.s (lhz r3,0xDA8(REG_ItemData))
    - Item hits use these fields for stale multiplier + stale queue update.
      refs/melee/src/melee/it/itcoll.c::it_80272460
      refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    """
    if 'attack_id' not in items_fixed.dtype.names or 'attack_instance' not in items_fixed.dtype.names:
        return
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_item_attack_fields is required; run `make build`') from exc
    laser_shot_kinds = _laser_shot_item_kinds(Path('data') / 'items' / 'lasers.bin')
    attack_id, attack_instance = msl_binding.derive_item_attack_fields(_ascontiguousarray(items_fixed['exists'], dtype=np.uint8), _ascontiguousarray(items_fixed['type'], dtype=np.uint16), _ascontiguousarray(items_fixed['owner'], dtype=np.int8), _ascontiguousarray(items_fixed['spawn_id'], dtype=np.uint32), _ascontiguousarray(fighter_attack_id, dtype=np.uint16), _ascontiguousarray(fighter_attack_instance, dtype=np.uint16), int(num_players), np.asarray(laser_shot_kinds, dtype=np.uint16))
    items_fixed['attack_id'] = attack_id
    items_fixed['attack_instance'] = attack_instance

def _derive_item_reflect_damage_mul(items_fixed: np.ndarray, *, post_action_id_u16: np.ndarray, post_char_id_u8: np.ndarray, post_state_flags_u8: np.ndarray, powershield_reflect_damage_mul: float, reflector_damage_mul_lut: np.ndarray, num_players: int) -> np.ndarray:
    """Derive per-item reflected-damage multiplier (`item->xC6C`) strictly causally.

    Decomp shape:
    - Reflect overlap stores per-item reflect multipliers on the item (`xC6C` damage, speed mul lane).
      refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    - Item apply path uses `(u32)(hit.damage * item->xC6C + 0.99f)`.
      refs/melee/src/melee/it/item.c::Item_80269F14
      refs/melee/src/melee/it/itcoll.c::it_80272460

    Causality contract:
    - Track each live item key `(spawn_id, type)` over replay frames.
    - On owner transfer, update that key's reflect multiplier from replay-visible reflector context:
      GuardReflect powershield lane or reflector character attrs.
    - Carry the value forward while the item stays alive.
    """
    n_frames = int(items_fixed.shape[0])
    if n_frames == 0:
        return np.ones((0, int(items_fixed.shape[1])), dtype=np.float32)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_item_reflect_damage_mul is required; run `make build`') from exc
    return msl_binding.derive_item_reflect_damage_mul(_ascontiguousarray(items_fixed['exists'], dtype=np.uint8), _ascontiguousarray(items_fixed['type'], dtype=np.uint16), _ascontiguousarray(items_fixed['owner'], dtype=np.int8), _ascontiguousarray(items_fixed['instance_id'], dtype=np.uint16), _ascontiguousarray(items_fixed['vel_x'], dtype=np.float32), _ascontiguousarray(items_fixed['vel_y'], dtype=np.float32), _ascontiguousarray(items_fixed['spawn_id'], dtype=np.uint32), _ascontiguousarray(post_action_id_u16, dtype=np.uint16), _ascontiguousarray(post_char_id_u8, dtype=np.uint8), _ascontiguousarray(post_state_flags_u8, dtype=np.uint8), _ascontiguousarray(reflector_damage_mul_lut, dtype=np.float32), float(powershield_reflect_damage_mul), int(num_players))

def _damage_hurt_height_from_action(action_id: int, on_ground: int) -> int:
    if action_id in (75, 78, 81):
        return 2
    if action_id in (76, 79, 82):
        return 1
    if action_id in (77, 80, 83):
        return 0
    if action_id in (84, 85, 86):
        return 1
    return 2 if int(on_ground) else 1

def _derive_item_hidden_callback_seed_lanes(*, seed_items: np.ndarray, ref_items: np.ndarray, seed_action_id_u16: np.ndarray, ref_action_id_u16: np.ndarray, seed_on_ground_u8: np.ndarray, ref_hitlag_u16: np.ndarray, ref_hitstun_u16: np.ndarray, ref_instance_hit_by_u16: np.ndarray, num_players: int, laser_types: tuple[int, ...], shield_bounce_types: tuple[int, ...]) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive hidden item callback/collision seed lanes for teacher-forced replay reseed.

    The represented state is item-internal and decomp-owned: pending reflect owner (`xC64/xC8C`),
    shield-bounce internals (`xC54/xC58/xDCE`), and the OnGiveDamage `xC34_damageDealt` latch.
    Slippi does not serialize those fields, so one-step replay seeds reconstruct them from the next
    exposed post-frame item/fighter state without branching on replay or record id.

    refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688,ftColl_80077C60}
    refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8,Item_8026A294}
    """
    n = int(seed_items.shape[0])
    slots = int(seed_items.shape[1])
    if n == 0:
        return (np.full((0, slots), 255, dtype=np.uint8), np.zeros((0, slots), dtype=np.uint16), np.zeros((0, slots), dtype=np.uint8), np.zeros((0, slots), dtype=np.float32), np.zeros((0, slots), dtype=np.float32), np.full((0, slots), 255, dtype=np.uint8), np.zeros((0, slots), dtype=np.uint8), np.zeros((0, slots), dtype=np.uint8))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_item_hidden_callback_seed_lanes is required; run `make build`') from exc
    laser_lut = _u8_lut_from_items(tuple((int(k) for k in laser_types)))
    shield_bounce_lut = _u8_lut_from_items(tuple((int(k) for k in shield_bounce_types)))
    return msl_binding.derive_item_hidden_callback_seed_lanes(_ascontiguousarray(seed_items['exists'], dtype=np.uint8), _ascontiguousarray(seed_items['type'], dtype=np.uint16), _ascontiguousarray(seed_items['owner'], dtype=np.int8), _ascontiguousarray(seed_items['instance_id'], dtype=np.uint16), _ascontiguousarray(seed_items['spawn_id'], dtype=np.uint32), _ascontiguousarray(seed_items['direction'], dtype=np.float32), _ascontiguousarray(seed_items['vel_x'], dtype=np.float32), _ascontiguousarray(seed_items['vel_y'], dtype=np.float32), _ascontiguousarray(ref_items['exists'], dtype=np.uint8), _ascontiguousarray(ref_items['type'], dtype=np.uint16), _ascontiguousarray(ref_items['owner'], dtype=np.int8), _ascontiguousarray(ref_items['instance_id'], dtype=np.uint16), _ascontiguousarray(ref_items['spawn_id'], dtype=np.uint32), _ascontiguousarray(ref_items['vel_x'], dtype=np.float32), _ascontiguousarray(ref_items['vel_y'], dtype=np.float32), _ascontiguousarray(seed_action_id_u16, dtype=np.uint16), _ascontiguousarray(ref_action_id_u16, dtype=np.uint16), _ascontiguousarray(ref_hitlag_u16, dtype=np.uint16), _ascontiguousarray(ref_hitstun_u16, dtype=np.uint16), _ascontiguousarray(ref_instance_hit_by_u16, dtype=np.uint16), laser_lut, shield_bounce_lut, int(num_players))
