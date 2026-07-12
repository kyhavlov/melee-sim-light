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
    MSL_MS_CLASS3_JUMP_COLL,
    MSL_MS_CLASS3_FALL_COLL,
    MSL_COLL_HANDLER_AIR_ESCAPE,
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

@functools.lru_cache(maxsize=None)
def _load_stage_segments_for_seed_cached(stage_id: int, data_root_text: str) -> tuple[dict, ...]:
    data_root = Path(data_root_text)
    stage_path = stage_metadata_path_for_stage_id(int(stage_id), data_root)
    if stage_path is None:
        return ()
    stage = _read_mslstg01(stage_path)
    out: list[dict] = []
    for seg in stage.segments:
        out.append({'i': int(seg.line_id), 'kind': _STAGE_KIND_BY_ID.get(int(seg.kind_id), 'dynamic'), 'platform': bool(int(seg.flags) & 1), 'ledge': bool(int(seg.flags) & 2), 'fighter_solid': bool(seg.fighter_solid), 'hi_flags': int(seg.hi_flags), 'lo_flags': int(seg.lo_flags), 'x0': float(seg.x0), 'y0': float(seg.y0), 'x1': float(seg.x1), 'y1': float(seg.y1)})
    return tuple(out)

def _load_stage_segments_for_seed(*, stage_id: int, data_root: Path) -> list[dict]:
    return list(_load_stage_segments_for_seed_cached(int(stage_id), _path_cache_key(data_root)))

@functools.cache
def _motion_state_owner_actions_by_char(data_root_text: str, *, class_bit: int=0, class3_bit: int=0, coll_handler_kind: int=-1, submotion_move_names: tuple[str, ...]=()) -> dict[int, frozenset[int]]:
    """Return PER-CHAR action-id sets from the generated MSLMSO01 owner rows.

    Replaces the old fox/falco INTERSECTION helper: the intersection was consumed
    char-blind, silently applying spacie-derived sets to every character's rows (marth
    aerial timings differ). Seed preprocessing receives action ids before runtime has
    loaded C owner helpers; use the same generated MotionState owner artifact here
    instead of local replay-shaped action-id lists.
    """
    owner_dir = Path(data_root_text) / 'motion_state' / 'owners'
    out: dict[int, frozenset[int]] = {}
    for cid, ch in manifest_registry_chars(Path(data_root_text)):
        owners = read_mslmso01_v1(owner_dir / f'{ch}.bin')
        wanted_submotions = set(_move_submotion_ids_for_char(data_root_text, ch, submotion_move_names) if submotion_move_names else ())
        selected: set[int] = set()
        for action_id in range(len(owners.submotion_id)):
            if class_bit and int(owners.class_bits[action_id]) & int(class_bit) == 0:
                continue
            if class3_bit and int(owners.class3_bits[action_id]) & int(class3_bit) == 0:
                continue
            if coll_handler_kind >= 0 and int(owners.coll_handler_kind[action_id]) != coll_handler_kind:
                continue
            if wanted_submotions and int(owners.submotion_id[action_id]) not in wanted_submotions:
                continue
            selected.add(action_id)
        out[int(cid)] = frozenset(selected)
    return out

@functools.cache
def _move_submotion_ids_for_char(data_root_text: str, char_name: str, move_names: tuple[str, ...]) -> tuple[int, ...]:
    moves_path = Path(data_root_text) / 'moves' / f'{char_name}.json'
    payload = _load_json_file(moves_path)
    moves = payload.get('moves', {})
    selected: set[int] = set()
    for name in move_names:
        row = moves.get(name)
        if isinstance(row, dict) and int(row.get('submotion_id', -1)) >= 0:
            selected.add(int(row['submotion_id']))
    return tuple(sorted(selected))

@functools.cache
def _attackair_first_hitbox_phase_by_char_action(data_root_text: str) -> dict[int, dict[int, tuple[int, int]]]:
    owner_dir = Path(data_root_text) / 'motion_state' / 'owners'
    by_char: dict[int, dict[int, tuple[int, int]]] = {}
    for cid, ch in manifest_registry_chars(Path(data_root_text)):
        owners = read_mslmso01_v1(owner_dir / f'{ch}.bin')
        moves_path = Path(data_root_text) / 'moves' / f'{ch}.json'
        moves = _load_json_file(moves_path).get('moves', {})
        phase_by_submotion: dict[int, tuple[int, int]] = {}
        for row in moves.values():
            if not isinstance(row, dict):
                continue
            submotion_id = int(row.get('submotion_id', -1))
            if submotion_id < 0:
                continue
            first_create = None
            first_clear = None
            for ev in row.get('events', []):
                if not isinstance(ev, dict):
                    continue
                kind = ev.get('kind')
                frame = int(ev.get('frame', -1))
                if kind == 'create_hitbox' and first_create is None:
                    first_create = frame
                elif kind == 'clear_hitboxes' and first_create is not None:
                    first_clear = frame
                    break
            if first_create is not None:
                phase_by_submotion[submotion_id] = (int(first_create), int(first_clear + 1 if first_clear is not None else 32767))
        selected: dict[int, tuple[int, int]] = {}
        for action_id in range(len(owners.submotion_id)):
            if int(owners.class_bits[action_id]) & int(MSL_MS_CLASS_ATTACK_AIR) == 0:
                continue
            phase = phase_by_submotion.get(int(owners.submotion_id[action_id]))
            if phase is not None:
                selected[action_id] = phase
        by_char[int(cid)] = selected
    return by_char

def _stage_ledge_floor_ids(*, stage_id: int, data_root: Path) -> tuple[int, int]:
    stage_path = stage_metadata_path_for_stage_id(int(stage_id), data_root)
    if stage_path is None:
        return (65535, 65535)
    stage = _read_mslstg01(stage_path)
    left_id = 65535
    right_id = 65535
    best_left_x = float('inf')
    best_right_x = -float('inf')
    for seg in stage.segments:
        if int(seg.kind_id) != 0 or not int(seg.flags) & 2:
            continue
        if float(seg.x0) < best_left_x:
            best_left_x = float(seg.x0)
            left_id = int(seg.line_id)
        if float(seg.x1) > best_right_x:
            best_right_x = float(seg.x1)
            right_id = int(seg.line_id)
    return (left_id, right_id)

def _fod_platform_heights_from_frames(frames, n_frames: int, *, default_heights: tuple[float, float]) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return replay-visible FoD platform heights, carried forward per frame.

    Slippi 3.18+ emits `fod_platform` events with platform id 0=right, 1=left and the current
    grIzumi platform height. Missing frames carry the previous height, matching the viewer parser's
    stage-state handling.
    refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    tools/viewer/slippi-viewer/src/parse/parser.ts::handleFodPlatformsEvent
    """
    default = np.asarray(default_heights, dtype=np.float32)
    if default.shape != (2,):
        raise ValueError(f'expected two FoD platform defaults, got shape {default.shape}')
    heights = np.zeros((n_frames, 2), dtype=np.float32)
    valid = np.zeros((n_frames, 2), dtype=np.uint8)
    fresh = np.zeros((n_frames, 2), dtype=np.uint8)
    heights[:, :] = default.reshape(1, 2)
    if frames.type.get_field_index('fod_platform') == -1:
        return (heights, valid, fresh)
    events = frames.field('fod_platform').to_pylist()
    cur = default.copy()
    cur_valid = np.zeros(2, dtype=np.uint8)
    for fi, lst in enumerate(events):
        if lst:
            for ev in lst:
                platform = int(ev.get('platform', -1))
                if 0 <= platform < 2:
                    cur[platform] = np.float32(float(ev.get('height', cur[platform])))
                    cur_valid[platform] = np.uint8(1)
                    fresh[fi, platform] = np.uint8(1)
        heights[fi, :] = cur
        valid[fi, :] = cur_valid
    return (heights, valid, fresh)

def _fod_platform_height_transform_records(data_root: Path | str=Path('data')) -> dict[int, tuple[int, float, float]]:
    """Return FoD platform line -> (platform id, height coeff, local y) from MSLSTG01.

    refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
    refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    """
    stage_path = stage_metadata_path_for_stage_id(2, Path(data_root))
    if stage_path is None:
        return {}
    stage = _read_mslstg01(stage_path)
    segment_y_by_line = {int(seg.line_id): float(seg.y0) for seg in stage.segments}
    out: dict[int, tuple[int, float, float]] = {}
    for rec in stage.platform_transforms:
        if int(rec.kind_id) != STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT:
            continue
        if 0 <= int(rec.platform_id) < 2 and float(rec.height_coeff) != 0.0:
            line_id = int(rec.line_id)
            out[line_id] = (int(rec.platform_id), float(rec.height_coeff), float(segment_y_by_line.get(line_id, 0.0)))
    return out

def _derive_fod_floor_skip_segments(*, action_id_u16: np.ndarray, action_frame_u16: np.ndarray, char_id_u8: np.ndarray, on_ground_u8: np.ndarray, pos_x_f32: np.ndarray, pos_y_f32: np.ndarray, speed_y_self_f32: np.ndarray, speed_y_attack_f32: np.ndarray, prev_main_y_i8: np.ndarray, main_y_i8: np.ndarray, platform_height_f32: np.ndarray, platform_height_valid_u8: np.ndarray, platform_air_land_stick_y_threshold: float, floor_skip_frames: int, data_root: Path | str=Path('data')) -> np.ndarray:
    """Derive prefix-causal hidden ``CollData.floor_skip`` for FoD platform pass-through.

    Slippi does not expose ``coll_data.floor_skip``. For replay/eval seeds, reconstruct only the
    current skipped FoD platform segment from frame-t state and current/prior input: a continuous
    down-held airborne callback episode whose self/KB displacement crosses a live transformed
    platform. JumpF/JumpB use ft_800835B0 and Fall uses ft_800831CC with the same
    ftCo_80096CC8 platform predicate as JumpAerial; after the initial crossing, carry that hidden
    floor-skip for the extracted x470 pass-through window. This avoids a runtime gameplay shortcut
    while preserving the source mpColl skip state needed by teacher-forced rows.

    refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    """
    stage_path = stage_metadata_path_for_stage_id(2, Path(data_root))
    if stage_path is None:
        return np.full(action_id_u16.shape, np.uint16(65535), dtype=np.uint16)
    stage = _read_mslstg01(stage_path)
    transforms = [rec for rec in stage.platform_transforms if int(rec.kind_id) == STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT and 0 <= int(rec.platform_id) < 2 and (float(rec.height_coeff) != 0.0)]
    if not transforms:
        return np.full(action_id_u16.shape, np.uint16(65535), dtype=np.uint16)
    data_root_path = Path(data_root)
    data_root_text = str(data_root_path)
    attackair_actions_by_char = _motion_state_owner_actions_by_char(data_root_text, class_bit=MSL_MS_CLASS_ATTACK_AIR)
    shallow_attackair_actions_by_char = _motion_state_owner_actions_by_char(data_root_text, class_bit=MSL_MS_CLASS_ATTACK_AIR, submotion_move_names=('ftCo_SM_AttackAirN', 'ftCo_SM_AttackAirHi', 'ftCo_SM_AttackAirLw'))
    attackair_first_phase_by_char_action = _attackair_first_hitbox_phase_by_char_action(data_root_text)
    escapeair_actions_by_char = _motion_state_owner_actions_by_char(data_root_text, coll_handler_kind=MSL_COLL_HANDLER_AIR_ESCAPE)
    jump_skip_actions_by_char = _motion_state_owner_actions_by_char(data_root_text, class3_bit=MSL_MS_CLASS3_JUMP_COLL)
    fall_skip_actions_by_char = _motion_state_owner_actions_by_char(data_root_text, class3_bit=MSL_MS_CLASS3_FALL_COLL)
    active_down_threshold_i8 = int(np.floor(float(platform_air_land_stick_y_threshold) * 127.0))
    jump_down_threshold_i8 = int(np.floor(float(platform_air_land_stick_y_threshold) * 80.0))
    segment_y_by_line = {int(seg.line_id): float(seg.y0) for seg in stage.segments}
    hard_floor_segments = [seg for seg in stage.segments if int(seg.kind_id) == 0 and bool(seg.fighter_solid) and (not bool(int(seg.flags) & 1))]
    max_action = int(np.max(action_id_u16, initial=0))
    for by_char in (attackair_actions_by_char, shallow_attackair_actions_by_char, escapeair_actions_by_char, jump_skip_actions_by_char, fall_skip_actions_by_char, attackair_first_phase_by_char_action):
        for inner in by_char.values():
            if inner:
                max_action = max(max_action, max((int(a) for a in inner)))
    lut_width = max_action + 1

    def _lut_from_actions(*maps: dict[int, set[int] | frozenset[int]]) -> np.ndarray:
        lut = np.zeros((256, lut_width), dtype=np.uint8)
        for mp in maps:
            for cid, actions in mp.items():
                if 0 <= int(cid) < 256:
                    for action in actions:
                        if 0 <= int(action) < lut_width:
                            lut[int(cid), int(action)] = np.uint8(1)
        return lut
    phase_start = np.full((256, lut_width), np.int16(-1), dtype=np.int16)
    phase_stop = np.full((256, lut_width), np.int16(-1), dtype=np.int16)
    for cid, by_action in attackair_first_phase_by_char_action.items():
        if not 0 <= int(cid) < 256:
            continue
        for action, phase in by_action.items():
            if 0 <= int(action) < lut_width:
                phase_start[int(cid), int(action)] = np.int16(int(phase[0]))
                phase_stop[int(cid), int(action)] = np.int16(int(phase[1]))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_fod_floor_skip_segments is required; run `make build`') from exc
    return msl_binding.derive_fod_floor_skip_segments(_ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(action_frame_u16, dtype=np.uint16), _ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(on_ground_u8, dtype=np.uint8), _ascontiguousarray(pos_x_f32, dtype=np.float32), _ascontiguousarray(pos_y_f32, dtype=np.float32), _ascontiguousarray(speed_y_self_f32, dtype=np.float32), _ascontiguousarray(speed_y_attack_f32, dtype=np.float32), _ascontiguousarray(prev_main_y_i8, dtype=np.int8), _ascontiguousarray(main_y_i8, dtype=np.int8), _ascontiguousarray(platform_height_f32, dtype=np.float32), _ascontiguousarray(platform_height_valid_u8, dtype=np.uint8), np.array([int(rec.line_id) for rec in transforms], dtype=np.uint16), np.array([int(rec.platform_id) for rec in transforms], dtype=np.uint8), np.array([float(rec.x0) for rec in transforms], dtype=np.float64), np.array([float(rec.x1) for rec in transforms], dtype=np.float64), np.array([float(rec.height_coeff) for rec in transforms], dtype=np.float64), np.array([float(segment_y_by_line.get(int(rec.line_id), 0.0)) for rec in transforms], dtype=np.float64), np.array([float(seg.x0) for seg in hard_floor_segments], dtype=np.float64), np.array([float(seg.y0) for seg in hard_floor_segments], dtype=np.float64), np.array([float(seg.x1) for seg in hard_floor_segments], dtype=np.float64), np.array([float(seg.y1) for seg in hard_floor_segments], dtype=np.float64), _lut_from_actions(attackair_actions_by_char, escapeair_actions_by_char), _lut_from_actions(attackair_actions_by_char), _lut_from_actions(jump_skip_actions_by_char, fall_skip_actions_by_char), _lut_from_actions(shallow_attackair_actions_by_char), phase_start, phase_stop, active_down_threshold_i8, jump_down_threshold_i8, int(floor_skip_frames))

def _derive_sheik_vanish_floor_skip_segments(*, stage_id: int, action_id_u16: np.ndarray, char_id_u8: np.ndarray, on_ground_u8: np.ndarray, ground_id_u16: np.ndarray, vanish_travel_timer_u8: np.ndarray, pos_x_f32: np.ndarray, pos_y_f32: np.ndarray, sheik_internal_id: int | None, travel_frames: int, ground_contact_min_frames: float, data_root: Path | str=Path('data')) -> np.ndarray:
    """Derive hidden CollData.floor_skip for Sheik Vanish Start1 platform pass-through.

    ftSk_SpecialAirHiStart_1_Coll increments mv.sk.specialhi.xC, then when accepted platform
    contact happens while xC < ftSeakAttributes::x3C, ftCo_8009A134 writes mpUpdateFloorSkip and
    leaves Sheik airborne. Slippi does not expose CollData.floor_skip, so subsequent one-step seeds
    need the same hidden platform id serialized from the prefix-visible Vanish travel episode.
    The frame-order state machine lives in native preprocessing; keep Python as the data-loader and
    wrapper only.

    refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
      ftSk_SpecialAirHiStart_1_Anim,ftSk_SpecialAirHiStart_1_Coll}
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    """
    try:
        import msl_binding
    except Exception as exc:
        raise RuntimeError('native msl_binding.derive_sheik_vanish_floor_skip_segments is required; run `make build`') from exc
    stage_segments = _load_stage_segments_for_seed(stage_id=int(stage_id), data_root=Path(data_root))
    platform_rows = [seg for seg in stage_segments if seg['kind'] == 'floor' and bool(seg['platform']) and bool(seg['fighter_solid'])]
    platform_segments = np.asarray([int(seg['i']) for seg in platform_rows], dtype=np.uint16)
    platform_x0 = np.asarray([float(seg['x0']) for seg in platform_rows], dtype=np.float32)
    platform_y0 = np.asarray([float(seg['y0']) for seg in platform_rows], dtype=np.float32)
    platform_x1 = np.asarray([float(seg['x1']) for seg in platform_rows], dtype=np.float32)
    platform_y1 = np.asarray([float(seg['y1']) for seg in platform_rows], dtype=np.float32)
    sheik_id = -1 if sheik_internal_id is None else int(sheik_internal_id)
    return msl_binding.derive_sheik_vanish_floor_skip_segments(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(on_ground_u8, dtype=np.uint8), _ascontiguousarray(ground_id_u16, dtype=np.uint16), _ascontiguousarray(vanish_travel_timer_u8, dtype=np.uint8), _ascontiguousarray(pos_x_f32, dtype=np.float32), _ascontiguousarray(pos_y_f32, dtype=np.float32), _ascontiguousarray(platform_segments, dtype=np.uint16), _ascontiguousarray(platform_x0, dtype=np.float32), _ascontiguousarray(platform_y0, dtype=np.float32), _ascontiguousarray(platform_x1, dtype=np.float32), _ascontiguousarray(platform_y1, dtype=np.float32), sheik_id, int(travel_frames), float(ground_contact_min_frames))

def _fod_platform_heights_with_ground_contact(heights: np.ndarray, valid: np.ndarray, *, post_on_ground_u8: np.ndarray, post_ground_id_u16: np.ndarray, post_pos_y_f32: np.ndarray, line_transforms: dict[int, tuple[int, float, float]]) -> tuple[np.ndarray, np.ndarray]:
    out_h, out_v, _, _ = _fod_platform_motion_with_ground_contact(heights, valid, post_on_ground_u8=post_on_ground_u8, post_ground_id_u16=post_ground_id_u16, post_pos_y_f32=post_pos_y_f32, line_transforms=line_transforms)
    return (out_h, out_v)

def _fod_platform_motion_with_ground_contact(heights: np.ndarray, valid: np.ndarray, *, event_fresh_u8: np.ndarray | None=None, post_on_ground_u8: np.ndarray, post_ground_id_u16: np.ndarray, post_pos_y_f32: np.ndarray, next_post_on_ground_u8: np.ndarray | None=None, next_post_ground_id_u16: np.ndarray | None=None, next_post_pos_y_f32: np.ndarray | None=None, line_transforms: dict[int, tuple[int, float, float]], motion_params: dict[str, float] | None=None, return_source: bool=False) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray] | tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Promote current FoD platform height from grounded replay-prefix contact.

    Some Slippi files lack the `fod_platform` event stream or have sparse current-height events.
    A grounded fighter on a FoD moving-platform line exposes the same current grIzumi/JObj height
    through the replay-visible root Y and MSLSTG01's line transform. Consecutive prefix contact also
    exposes the current per-frame height delta, which is the causal grIzumi state needed to keep
    transformed platform floors moving during replay rollout.

    When supplied, ``next_post_*`` is a teacher-forced same-step hidden-state reconstruction: a
    frame-i collision that lands on a transformed FoD platform exposes the already-updated grIzumi
    platform height only in post-frame i+1. That value initializes the frame-i collision seed only
    when no nonzero platform velocity owner is active, so runtime/free-running gameplay remains
    owned by the stage scheduler and moving-platform rows do not double-advance.

    refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    """
    heights_arr = _ascontiguousarray(heights, dtype=np.float32)
    valid_arr = _ascontiguousarray(valid, dtype=np.uint8)
    event_fresh = None if event_fresh_u8 is None else _ascontiguousarray(event_fresh_u8, dtype=np.uint8)
    on_ground = _ascontiguousarray(post_on_ground_u8, dtype=np.uint8)
    ground_id = _ascontiguousarray(post_ground_id_u16, dtype=np.uint16)
    pos_y = _ascontiguousarray(post_pos_y_f32, dtype=np.float32)
    if heights_arr.shape != valid_arr.shape or heights_arr.ndim != 2 or heights_arr.shape[1] != 2:
        raise ValueError('FoD platform height arrays must have shape [frames, 2]')
    if on_ground.shape != ground_id.shape or on_ground.shape != pos_y.shape:
        raise ValueError('FoD grounded-contact arrays must have matching shapes')
    if on_ground.shape[0] != heights_arr.shape[0]:
        raise ValueError('FoD grounded-contact frame count must match height frame count')
    if event_fresh is not None and event_fresh.shape != heights_arr.shape:
        raise ValueError('FoD event-fresh array must match height shape')
    next_on_ground = None if next_post_on_ground_u8 is None else _ascontiguousarray(next_post_on_ground_u8, dtype=np.uint8)
    next_ground_id = None if next_post_ground_id_u16 is None else _ascontiguousarray(next_post_ground_id_u16, dtype=np.uint16)
    next_pos_y = None if next_post_pos_y_f32 is None else _ascontiguousarray(next_post_pos_y_f32, dtype=np.float32)
    if (next_on_ground is None) != (next_ground_id is None) or (next_on_ground is None) != (next_pos_y is None):
        raise ValueError('FoD next-post grounded-contact arrays must be supplied together')
    if next_on_ground is not None:
        if next_on_ground.shape != next_ground_id.shape or next_on_ground.shape != next_pos_y.shape:
            raise ValueError('FoD next-post grounded-contact arrays must have matching shapes')
        if next_on_ground.shape != on_ground.shape:
            raise ValueError('FoD next-post grounded-contact arrays must match seed frame shape')
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_fod_platform_motion_with_ground_contact is required; run `make build`') from exc
    if line_transforms:
        items = sorted(line_transforms.items())
        line_ids = np.array([line_id for line_id, _ in items], dtype=np.uint16)
        platform_ids = np.array([rec[0] for _, rec in items], dtype=np.uint8)
        height_coeff = np.array([rec[1] for _, rec in items], dtype=np.float64)
        local_y = np.array([rec[2] for _, rec in items], dtype=np.float64)
    else:
        line_ids = np.zeros(0, dtype=np.uint16)
        platform_ids = np.zeros(0, dtype=np.uint8)
        height_coeff = np.zeros(0, dtype=np.float64)
        local_y = np.zeros(0, dtype=np.float64)
    use_motion_params = motion_params is not None
    home = float(0.0 if motion_params is None else motion_params['home_height'])
    max_h = float(0.0 if motion_params is None else motion_params['max_height'])
    min_visible = float(0.0 if motion_params is None else motion_params['min_visible_height'])
    hidden = float(0.0 if motion_params is None else motion_params['hidden_target_height'])
    return msl_binding.derive_fod_platform_motion_with_ground_contact(heights_arr, valid_arr, None if event_fresh is None else event_fresh, on_ground, ground_id, pos_y, None if next_on_ground is None else next_on_ground, None if next_ground_id is None else next_ground_id, None if next_pos_y is None else next_pos_y, line_ids, platform_ids, height_coeff, local_y, int(use_motion_params), home, max_h, min_visible, hidden, int(return_source))

def _fod_hidden_return_timers(heights: np.ndarray, valid: np.ndarray, *, motion_params: dict[str, float] | None) -> tuple[np.ndarray, np.ndarray]:
    """Derive hidden grIzumi return countdowns for replay rollout seeds.

    When a FoD side platform is parked at the generated hidden target, replay rows expose the
    current hidden height but not grIzumi's hidden wait timer. The next source-visible upward
    height lets us reconstruct the current phase-4 countdown without trusting a stale sparse
    collision height as current source authority.

    refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    data/stages/bin/griz.bin::MSLSTG01 platform_motions
    """
    h = np.asarray(heights, dtype=np.float32)
    v = np.asarray(valid, dtype=np.uint8)
    out_timer = np.zeros(h.shape, dtype=np.uint16)
    out_valid = np.zeros(v.shape, dtype=np.uint8)
    if motion_params is None or h.ndim != 2 or h.shape[1] != 2 or (v.shape != h.shape):
        return (out_timer, out_valid)
    hidden = float(motion_params['hidden_target_height'])
    up_speed = float(motion_params['up_speed'])
    if not np.isfinite(hidden) or not np.isfinite(up_speed) or up_speed <= 0.0:
        return (out_timer, out_valid)
    hidden_eps = 0.001
    move_eps = max(0.001, 0.25 * up_speed)
    max_timer = np.iinfo(np.uint16).max
    for platform_id in range(2):
        col_h = h[:, platform_id]
        col_valid = (v[:, platform_id] != 0) & np.isfinite(col_h)
        hidden_rows = col_valid & (np.abs(col_h - hidden) <= hidden_eps)
        moved_rows = col_valid & (col_h > hidden + move_eps)
        candidate_rows = hidden_rows | moved_rows
        if not np.any(hidden_rows) or not np.any(moved_rows):
            continue
        breaks = np.flatnonzero(~candidate_rows)
        starts = np.concatenate(([0], breaks + 1))
        stops = np.concatenate((breaks, [col_h.shape[0]]))
        for start, stop in zip(starts, stops, strict=True):
            if start >= stop:
                continue
            seg = slice(int(start), int(stop))
            seg_hidden_rel = np.flatnonzero(hidden_rows[seg])
            seg_moved_rel = np.flatnonzero(moved_rows[seg])
            if seg_hidden_rel.size == 0 or seg_moved_rel.size == 0:
                continue
            moved_abs = seg_moved_rel + int(start)
            moved_frames = np.maximum(1, np.rint((col_h[moved_abs] - hidden) / up_speed).astype(np.int64))
            first_move_abs = np.maximum(0, moved_abs.astype(np.int64) - moved_frames)
            hidden_abs = seg_hidden_rel + int(start)
            next_moved_idx = np.searchsorted(moved_abs, hidden_abs, side='right')
            has_next = next_moved_idx < moved_abs.size
            if not np.any(has_next):
                continue
            hidden_abs = hidden_abs[has_next]
            next_first_move = first_move_abs[next_moved_idx[has_next]]
            timers = next_first_move - hidden_abs.astype(np.int64) - 1
            ok = (timers >= 0) & (timers <= max_timer)
            if np.any(ok):
                out_timer[hidden_abs[ok], platform_id] = timers[ok].astype(np.uint16)
                out_valid[hidden_abs[ok], platform_id] = np.uint8(1)
    return (out_timer, out_valid)

def _fod_visible_choice_lanes(heights: np.ndarray, valid: np.ndarray, fresh: np.ndarray | None, frame_pre_random_seed: np.ndarray, *, motion_params: dict[str, float] | None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Derive a bounded FoD visible-move choice seed from direct platform events.

    Slippi direct `fod_platform` events expose the source grIzumi trajectory. When a platform parks
    at home and later emits a fresh direct downward movement, the source has already sampled the
    visible wait and the choice/target RNG for that episode. Carry only that direct-event-proven
    episode seed so replay rollout can reproduce the same FoD choice without changing ordinary
    free-running scheduler behavior.

    refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    """
    h = np.asarray(heights, dtype=np.float32)
    v = np.asarray(valid, dtype=np.uint8)
    seed = np.asarray(frame_pre_random_seed, dtype=np.uint32)
    out_timer = np.zeros(h.shape, dtype=np.uint16)
    out_valid = np.zeros(v.shape, dtype=np.uint8)
    out_rng = np.zeros(h.shape, dtype=np.uint32)
    if motion_params is None or fresh is None or h.ndim != 2 or (h.shape[1] != 2) or (v.shape != h.shape) or (fresh.shape != h.shape) or (seed.shape[0] != h.shape[0]):
        return (out_timer, out_valid, out_rng)
    home = float(motion_params['home_height'])
    down_speed = float(motion_params['down_speed'])
    eps = max(0.001, 0.25 * abs(down_speed))
    for platform_id in range(2):
        event_mask = (v[:, platform_id] != 0) & (fresh[:, platform_id] != 0)
        event_idx = np.flatnonzero(event_mask)
        if event_idx.size == 0:
            continue
        event_h = h[event_idx, platform_id]
        home_events = event_idx[np.abs(event_h - home) <= eps].astype(np.int64)
        move_events = event_idx[event_h < home - eps].astype(np.int64)
        if home_events.size == 0 or move_events.size == 0:
            continue
        prev_home_pos = np.searchsorted(home_events, move_events, side='right') - 1
        has_home = prev_home_pos >= 0
        if not np.any(has_home):
            continue
        move_events = move_events[has_home]
        paired_home = home_events[prev_home_pos[has_home]]
        _, first_for_home = np.unique(paired_home, return_index=True)
        paired_home = paired_home[first_for_home]
        move_events = move_events[first_for_home]
        branch_frame = move_events - 1
        timers = move_events - paired_home - 4
        ok = (branch_frame > paired_home) & (branch_frame < h.shape[0]) & (timers >= 0) & (timers <= np.iinfo(np.uint16).max)
        if not np.any(ok):
            continue
        paired_home = paired_home[ok]
        move_events = move_events[ok]
        timers = timers[ok]
        branch_frame = branch_frame[ok]
        latest = int(np.argmax(paired_home))
        rows = slice(0, int(paired_home[latest]) + 1)
        out_timer[rows, platform_id] = np.uint16(timers[latest])
        out_rng[rows, platform_id] = np.uint32(seed[int(branch_frame[latest])])
        out_valid[rows, platform_id] = np.uint8(1)
    return (out_timer, out_valid, out_rng)

def _dir_to_facing(direction: np.ndarray) -> np.ndarray:
    return (direction > 0).astype(np.uint8)

def _airborne_to_on_ground(airborne: np.ndarray | None, n: int) -> np.ndarray:
    if airborne is None:
        return np.zeros(n, dtype=np.uint8)
    return (airborne == 0).astype(np.uint8)

def _post_position_z(post, n_frames: int) -> np.ndarray:
    post_pos = post.field('position')
    if post_pos.type.get_field_index('z') != -1:
        return _to_numpy(post_pos.field('z')).astype(np.float32)
    if post.type.get_field_index('position_z') != -1:
        return _to_numpy(post.field('position_z')).astype(np.float32)
    if post.type.get_field_index('pos_z') != -1:
        return _to_numpy(post.field('pos_z')).astype(np.float32)
    return np.zeros(n_frames, dtype=np.float32)

def _derive_grounded_overlap_hidden_pos_z(*, num_players: int, char_id_u8: np.ndarray, action_id_u16: np.ndarray, on_ground_u8: np.ndarray, stocks_u8: np.ndarray, pos_x_f32: np.ndarray, pos_z_f32: np.ndarray, facing_u8: np.ndarray, common: dict, data_dir: str='data') -> np.ndarray:
    """Strictly causal hidden depth lane for ftCommon_8007DD7C/ftCommon_8007E0E4.

    Slippi commonly reports fighter `pos_z` as zero even while the engine's grounded fighter-overlap
    nudge lane carries nonzero depth. This reconstructs that hidden lane from replay prefix state
    and extracted pushbox/common data, without consulting future combat outcomes.

    Decomp/data anchors:
    - refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    - refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    - data/common/ft_common_data.json::{player_nudge_z,player_nudge_z_max}
    - data/characters/{fox,falco}.json::{pushbox_x,pushbox_y}
    """
    char = np.asarray(char_id_u8, dtype=np.uint8)
    action = np.asarray(action_id_u16, dtype=np.uint16)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8)
    stocks = np.asarray(stocks_u8, dtype=np.uint8)
    pos_x = np.asarray(pos_x_f32, dtype=np.float32)
    pos_z = np.asarray(pos_z_f32, dtype=np.float32)
    facing = np.asarray(facing_u8, dtype=np.uint8)
    if not char.shape == action.shape == on_ground.shape == stocks.shape == pos_x.shape == pos_z.shape == facing.shape:
        raise ValueError('hidden pos_z inputs must have matching shape')
    push_x = np.zeros(256, dtype=np.float32)
    push_y = np.zeros(256, dtype=np.float32)
    from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS_PB
    char_files = {info.internal_id: info.name for info in _REGISTRY_CHARS_PB.values()}
    root = Path(data_dir)
    for cid, key in char_files.items():
        path = root / 'characters' / f'{key}.json'
        if not path.exists():
            continue
        data = _load_json_file(path)
        push_x[cid] = np.float32(float(data.get('pushbox_x', 0.0)))
        push_y[cid] = np.float32(float(data.get('pushbox_y', 0.0)))
    step = np.float32(float(common.get('player_nudge_z', 0.0)))
    z_max = np.float32(float(common.get('player_nudge_z_max', 0.0)))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_grounded_overlap_hidden_pos_z is required; run `make build`') from exc
    return msl_binding.derive_grounded_overlap_hidden_pos_z(int(num_players), char, action, on_ground, stocks, pos_x, pos_z, facing, push_x, push_y, float(step), float(z_max))

def _u8_from_float01(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0.0, 1.0)
    return np.round(x * 255.0).astype(np.uint8)

def _int8_from_float_axis(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, -1.0, 1.0)
    return np.round(x * 127.0).astype(np.int8)

def _stick_i8_from_unit_stick(x: np.ndarray) -> np.ndarray:
    STICK_MAX_I8 = np.float32(80.0)
    x = np.clip(x, -1.0, 1.0)
    return np.trunc(x * STICK_MAX_I8).astype(np.int8)

def _u16_from_float_frames(x: np.ndarray | None, n: int) -> np.ndarray:
    if x is None:
        return np.zeros(n, dtype=np.uint16)
    x = np.clip(x, 0.0, 65535.0)
    return np.floor(x).astype(np.uint16)

def _i16_from_state_age(state_age: np.ndarray | None, n: int) -> np.ndarray:
    if state_age is None:
        return np.zeros(n, dtype=np.int16)
    x = np.clip(state_age, -32768.0, 32767.0)
    return np.floor(x).astype(np.int16)

def _f32_from_state_age(state_age: np.ndarray | None, n: int) -> np.ndarray:
    if state_age is None:
        return np.zeros(n, dtype=np.float32)
    return state_age.astype(np.float32)
