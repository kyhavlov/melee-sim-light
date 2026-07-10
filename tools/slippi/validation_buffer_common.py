from __future__ import annotations
import functools
import json
import struct  # noqa: F401
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace  # noqa: F401
from typing import Any
import numpy as np
import pyarrow as pa  # noqa: F401
from peppi_py import _read_slippi  # noqa: F401
from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.action_state_tables import load_action_state_tables  # noqa: F401
from tools.slippi.hitstun import hitstun_u16_from_misc_as_and_state_flags3  # noqa: F401
from tools.slippi.item_article_data import item_article_kind_set, item_article_values_by_sim_char  # noqa: F401
from tools.extraction.known_data_artifacts import STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT, dream_whispy_metadata, fountain_of_dreams_default_platform_heights, fountain_of_dreams_platform_motion_params, read_mslstg01_v7, stage_metadata_path_for_stage_id, yoshi_shyguy_metadata  # noqa: F401
from tools.slippi.motion_state_owners import read_callback_manifest, read_mslmso01_v1  # noqa: F401
from tools.slippi.rollback import finalized_frame_indices  # noqa: F401
from tools.slippi.slpz import replay_path_for_peppi  # noqa: F401
from tools.extraction.known_data_artifacts import read_mslftsc1_v1  # noqa: F401
from tools.slippi.suite_io import team_attack_on_from_start  # noqa: F401
MSL_MS_CLASS_ATTACK_AIR = 1 << 0
FOD_SKIP_ECB_VERTICAL_UNIT = 1.0
FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP = 2.0 * FOD_SKIP_ECB_VERTICAL_UNIT
FOD_FLOOR_X_END_CLAMP = 0.1
FOD_FLOOR_Y_BIAS = 0.0001
FOD_STAGE_LINE_DX_EPSILON = 1e-06
_STAGE_KIND_BY_ID = {0: 'floor', 1: 'ceiling', 2: 'right_wall', 3: 'left_wall', 4: 'dynamic'}
_MATCH_FLOW_ACTION_IDS = {0, 1, 2, 4, 6, 7, 8, 9, 10, 12, 13, 322, 323, 324}

def _ascontiguousarray(arr: Any, dtype: Any=None) -> np.ndarray:
    if isinstance(arr, np.ndarray):
        want = None if dtype is None else np.dtype(dtype)
        if arr.flags.c_contiguous and (want is None or arr.dtype == want):
            return arr
    return np.ascontiguousarray(arr, dtype=dtype)

@functools.lru_cache(maxsize=64)
def _u8_lut_from_items(items: tuple[int, ...]) -> np.ndarray:
    lut = np.zeros(65536, dtype=np.uint8)
    for item in items:
        lut[int(item) & 65535] = np.uint8(1)
    return lut

@functools.lru_cache(maxsize=64)
def _u8_lut_from_pairs(pairs: tuple[tuple[int, int], ...]) -> np.ndarray:
    lut = np.zeros(65536, dtype=np.uint8)
    for key, value in pairs:
        lut[int(key) & 65535] = np.uint8(value)
    return lut

def _path_cache_key(path: Path) -> str:
    return str(path)

@functools.lru_cache(maxsize=None)
def _load_json_file_cached(path_text: str) -> Any:
    return json.loads(Path(path_text).read_text(encoding='utf-8'))

@functools.lru_cache(maxsize=None)
def _load_bytes_file_cached(path_text: str) -> bytes:
    return Path(path_text).read_bytes()

@functools.lru_cache(maxsize=None)
def _read_mslstg01_v7_cached(path_text: str):
    return read_mslstg01_v7(Path(path_text))

def _load_json_file(path: Path) -> Any:
    return _load_json_file_cached(_path_cache_key(path))

def _load_bytes_file(path: Path) -> bytes:
    return _load_bytes_file_cached(_path_cache_key(path))

def _read_mslstg01(path: Path):
    return _read_mslstg01_v7_cached(_path_cache_key(path))

@dataclass
class ValidationReplayBuffers:
    seed_t: np.ndarray
    prev_input_t: np.ndarray
    input_t: np.ndarray
    ref_t1: np.ndarray
    num_players: int

    @property
    def num_records(self) -> int:
        return int(self.seed_t.shape[0])

    def seed_u8(self) -> np.ndarray:
        return self.seed_t.view(np.uint8).reshape(self.num_records, self.seed_t.dtype.itemsize)

    def prev_input_u8(self) -> np.ndarray:
        return self.prev_input_t.view(np.uint8).reshape(self.num_records, self.prev_input_t.dtype.itemsize)

    def input_u8(self) -> np.ndarray:
        return self.input_t.view(np.uint8).reshape(self.num_records, self.input_t.dtype.itemsize)

    def ref_u8(self) -> np.ndarray:
        return self.ref_t1.view(np.uint8).reshape(self.num_records, self.ref_t1.dtype.itemsize)

class _SampleParts:

    def __init__(self, n: int) -> None:
        self.seed_t = np.zeros(n, dtype=SEED_DTYPE)
        self.prev_input_t = np.zeros(n, dtype=INPUT_DTYPE)
        self.input_t = np.zeros(n, dtype=INPUT_DTYPE)
        self.ref_t1 = np.zeros(n, dtype=COMPARE_DTYPE)
        self.shape = (n,)

    def __getitem__(self, key: str) -> np.ndarray:
        if key == 'seed_t':
            return self.seed_t
        if key == 'prev_input_t':
            return self.prev_input_t
        if key == 'input_t':
            return self.input_t
        if key == 'ref_t1':
            return self.ref_t1
        raise KeyError(key)

    def as_buffers(self, *, num_players: int) -> ValidationReplayBuffers:
        return ValidationReplayBuffers(seed_t=self.seed_t, prev_input_t=self.prev_input_t, input_t=self.input_t, ref_t1=self.ref_t1, num_players=int(num_players))

    def seed_u8(self) -> np.ndarray:
        return self.seed_t.view(np.uint8).reshape(self.shape[0], self.seed_t.dtype.itemsize)

    def prev_input_u8(self) -> np.ndarray:
        return self.prev_input_t.view(np.uint8).reshape(self.shape[0], self.prev_input_t.dtype.itemsize)

    def input_u8(self) -> np.ndarray:
        return self.input_t.view(np.uint8).reshape(self.shape[0], self.input_t.dtype.itemsize)

    def ref_u8(self) -> np.ndarray:
        return self.ref_t1.view(np.uint8).reshape(self.shape[0], self.ref_t1.dtype.itemsize)

@dataclass(frozen=True)
class _ManifestPreprocessTables:
    manifest_chars: tuple[tuple[int, str], ...]
    char_landing_air_lag_frames: dict[int, dict[str, int]]
    char_fallspecial_origin_lag: dict[int, dict[int, float]]
    char_fallspecial_origin_allow_interrupt: dict[int, dict[int, tuple[int, int]]]
    char_walk_divisors: dict[int, tuple[float, float, float]]
    char_walk_max: dict[int, float]
    char_run_scaling: dict[int, float]
    char_gr_friction: dict[int, float]
    char_gr_friction_lut: np.ndarray
    char_rebound_anim_numerator_frames: dict[int, float]
    rebound_numerator_lut: np.ndarray
    char_can_walljump: dict[int, bool]
    char_walljump_setup_x_delta_threshold: dict[int, float]
    active_shield_hit_lut: np.ndarray
    sheik_char_id: int
    zelda_char_id: int
    sheik_vanish_travel_frames: int
    sheik_vanish_ground_contact_min_frames: float
    zelda_farore_travel_frames: int
    zelda_farore_ground_contact_min_frames: float
    sheik_chain_release_min_frames: int

def _env_damage_int(dmg: float) -> int:
    if float(dmg) == 0.0:
        return 0
    i = int(dmg)
    return i if i != 0 else 1

def _load_character_attrs(data_root: Path, name: str) -> dict[str, Any]:
    return _load_json_file(Path(data_root) / 'characters' / f'{name}.json')

def _load_moves_file(data_root: Path, name: str) -> dict[str, Any]:
    return _load_json_file(Path(data_root) / 'moves' / f'{name}.json')

def _load_common_data(data_root: Path=Path('data')) -> dict[str, Any]:
    return _load_json_file(Path(data_root) / 'common' / 'ft_common_data.json')

def manifest_registry_chars(data_root: Path) -> list[tuple[int, str]]:
    """(internal_id, name) for every char in the data manifest, registry-verified.

    Fails loudly when manifest.json names a char missing from the extraction registry:
    silently skipping is exactly the per-char no-op class the de-spacie pass eliminated
    (a skipped char gets default/empty entries in every per-char preprocessor map).
    """
    from tools.extraction.char_registry import CHARS as registry_chars
    manifest_chars = json.loads((data_root / 'manifest.json').read_text()).get('chars') or ['fox', 'falco']
    out: list[tuple[int, str]] = []
    for key in manifest_chars:
        info = registry_chars.get(str(key))
        if info is None:
            raise ValueError(f'data manifest names char {key!r} not present in tools/extraction/char_registry.py - add the registry row before preprocessing')
        out.append((info.internal_id, info.name))
    return out

def require_replay_chars_in_manifest(replay_char_ids: 'np.ndarray', manifest_chars: list[tuple[int, str]]) -> None:
    """Refuse to build validation buffers for a replay whose character is absent from the manifest.

    A manifest without the replay's character means every per-character preprocessing map
    silently resolved empty for it (landing lag, walk/run anim rates, friction, motion-state
    owners) - validation buffers would build "successfully" with subtly wrong seed lanes.
    """
    manifest_ids = {cid for cid, _key in manifest_chars}
    vals = np.asarray(replay_char_ids)
    if np.issubdtype(vals.dtype, np.floating):
        vals = vals[np.isfinite(vals)]
    missing = sorted(set((int(c) for c in np.unique(vals))) - manifest_ids)
    if missing:
        raise ValueError(f'replay uses char internal id(s) {missing} not in data manifest chars {sorted(manifest_ids)} - regenerate data with `make build_data` (registry-driven) or add the character to tools/extraction/char_registry.py first')

@functools.lru_cache(maxsize=None)
def _manifest_preprocess_tables(data_root_text: str) -> _ManifestPreprocessTables:
    data_root = Path(data_root_text)
    manifest_chars = tuple(manifest_registry_chars(data_root))
    char_landing_air_lag_frames: dict[int, dict[str, int]] = {}
    char_fallspecial_origin_lag: dict[int, dict[int, float]] = {}
    char_fallspecial_origin_allow_interrupt: dict[int, dict[int, tuple[int, int]]] = {}
    char_walk_divisors: dict[int, tuple[float, float, float]] = {}
    char_walk_max: dict[int, float] = {}
    char_run_scaling: dict[int, float] = {}
    char_gr_friction: dict[int, float] = {}
    char_rebound_anim_numerator_frames: dict[int, float] = {}
    char_can_walljump: dict[int, bool] = {}
    char_walljump_setup_x_delta_threshold: dict[int, float] = {}
    char_active_shield_hit_int_damage: dict[int, dict[int, dict[int, int]]] = {}
    sheik_char_id = -1
    zelda_char_id = -1
    sheik_vanish_travel_frames = 0
    sheik_vanish_ground_contact_min_frames = 0.0
    zelda_farore_travel_frames = 0
    zelda_farore_ground_contact_min_frames = 0.0
    sheik_chain_release_min_frames = 0
    for cid, key in manifest_chars:
        attrs = _load_character_attrs(data_root, key)
        if key == 'sheik':
            sheik_char_id = int(cid)
            sheik_vanish_travel_frames = int(attrs.get('sheik_vanish_travel_frames', 0))
            sheik_vanish_ground_contact_min_frames = float(attrs.get('sheik_vanish_ground_contact_min_frames', 0.0))
            sheik_chain_release_min_frames = int(attrs.get('sheik_chain_release_min_frames', 0))
        elif key == 'zelda':
            zelda_char_id = int(cid)
            zelda_farore_travel_frames = int(attrs.get('zelda_farore_travel_frames', 0))
            zelda_farore_ground_contact_min_frames = float(attrs.get('zelda_farore_ground_contact_min_frames', 0.0))
        move_file = _load_moves_file(data_root, key)
        move_data = move_file['moves']
        special_move_data = move_file.get('specials_by_msid', {})
        char_landing_air_lag_frames[int(cid)] = {'airn': int(attrs['landing_airn_lag_frames']), 'airf': int(attrs['landing_airf_lag_frames']), 'airb': int(attrs['landing_airb_lag_frames']), 'airhi': int(attrs['landing_airhi_lag_frames']), 'airlw': int(attrs['landing_airlw_lag_frames'])}
        origin_lag: dict[int, float] = {}
        origin_allow_interrupt: dict[int, tuple[int, int]] = {}
        owners_tbl = read_mslmso01_v1(data_root / 'motion_state' / 'owners' / f'{key}.bin')
        if 'illusion_landing_lag_frames' in attrs or 'firefox_landing_lag_frames' in attrs:
            from tools.extraction.extract_motion_state_owners import FX_SPECIAL_KIND_VALUES
            illusion_kind = FX_SPECIAL_KIND_VALUES['SPECIAL_AIR_S_END']
            firefox_kinds = {FX_SPECIAL_KIND_VALUES['SPECIAL_AIR_HI'], FX_SPECIAL_KIND_VALUES['SPECIAL_HI_FALL'], FX_SPECIAL_KIND_VALUES['SPECIAL_HI_BOUND']}
            for a in range(len(owners_tbl.fx_special_kind)):
                k = int(owners_tbl.fx_special_kind[a])
                if k == illusion_kind and 'illusion_landing_lag_frames' in attrs:
                    origin_lag[a] = float(attrs['illusion_landing_lag_frames'])
                    origin_allow_interrupt[a] = (1, 0)
                elif k in firefox_kinds and 'firefox_landing_lag_frames' in attrs:
                    origin_lag[a] = float(attrs['firefox_landing_lag_frames'])
                    direct_kinds = {FX_SPECIAL_KIND_VALUES['SPECIAL_HI_FALL'], FX_SPECIAL_KIND_VALUES['SPECIAL_HI_BOUND']}
                    origin_allow_interrupt[a] = (1, 1 if k in direct_kinds else 0)
        if 'specialhi_landing_lag_frames' in attrs:
            sm_path = data_root / 'special_msids' / f'{key}.json'
            if sm_path.exists():
                sm = _load_json_file(sm_path)
                up_msids = set()
                for up_key in ('up_air', 'up_ground'):
                    main = (sm.get(up_key) or {}).get('main') or {}
                    for v in main.values():
                        if isinstance(v, int):
                            up_msids.add(int(v))
                for a in range(len(owners_tbl.submotion_id)):
                    if int(owners_tbl.submotion_id[a]) in up_msids:
                        origin_lag[a] = float(attrs['specialhi_landing_lag_frames'])
                        origin_allow_interrupt[a] = (0, 0)
        if 'falcon_specialhi_landing_lag' in attrs:
            # Falcon freefall origins all enter through ftCo_80096900(..., allow_interrupt=false):
            # Special(Air)Hi whiff -> specialhi_landing_lag, SpecialAirSStart (air Raptor miss) ->
            # specials_miss_landing_lag, SpecialAirS (air Raptor hit) -> specials_hit_landing_lag.
            # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_Anim,
            #   ftCa_SpecialAirHi_Anim}
            # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::{
            #   ftCa_SpecialAirSStart_Anim,ftCa_SpecialAirS_Anim}
            sm_path = data_root / 'special_msids' / f'{key}.json'
            if sm_path.exists():
                sm = _load_json_file(sm_path)

                def _msids(group: str, slot: str) -> set[int]:
                    vals = (sm.get(group) or {}).get(slot) or {}
                    return {int(v) for v in vals.values() if isinstance(v, int)}

                lag_by_msid: dict[int, float] = {}
                for m in _msids('up_air', 'main') | _msids('up_ground', 'main'):
                    lag_by_msid[m] = float(attrs['falcon_specialhi_landing_lag'])
                for m in _msids('side_air', 'start'):
                    lag_by_msid[m] = float(attrs['falcon_specials_miss_landing_lag'])
                for m in _msids('side_air', 'main'):
                    lag_by_msid[m] = float(attrs['falcon_specials_hit_landing_lag'])
                for a in range(len(owners_tbl.submotion_id)):
                    lag = lag_by_msid.get(int(owners_tbl.submotion_id[a]))
                    if lag is not None and lag > 0.0:
                        origin_lag[a] = lag
                        origin_allow_interrupt[a] = (0, 0)
        char_fallspecial_origin_lag[int(cid)] = origin_lag
        char_fallspecial_origin_allow_interrupt[int(cid)] = origin_allow_interrupt
        char_walk_divisors[int(cid)] = (float(attrs['slow_walk_max']), float(attrs['mid_walk_point']), float(attrs['fast_walk_min']))
        char_walk_max[int(cid)] = float(attrs['walk_max_vel'])
        char_run_scaling[int(cid)] = float(attrs['run_animation_scaling'])
        char_gr_friction[int(cid)] = float(attrs['gr_friction'])
        char_rebound_anim_numerator_frames[int(cid)] = float(attrs['rebound_anim_numerator_frames'])
        char_can_walljump[int(cid)] = bool(attrs.get('can_walljump', False))
        char_walljump_setup_x_delta_threshold[int(cid)] = float(attrs['walljump_setup_x_delta_threshold'])
        active_int_damage_by_anim: dict[int, dict[int, int]] = {}
        for move in [*move_data.values(), *special_move_data.values()]:
            submotion_id = int(move.get('submotion_id', -1))
            if submotion_id < 0:
                continue
            events = sorted(move.get('events', []), key=lambda ev: (int(ev.get('frame', 0)), ev.get('kind', '')))
            events_by_frame: dict[int, list[dict]] = {}
            max_frame = 0
            for ev in events:
                frame = int(ev.get('frame', 0))
                events_by_frame.setdefault(frame, []).append(ev)
                max_frame = max(max_frame, frame)
            active_by_hitbox: dict[int, int] = {}
            frame_damage: dict[int, int] = {}
            for frame in range(0, max_frame + 2):
                for ev in events_by_frame.get(frame, []):
                    kind = ev.get('kind')
                    if kind == 'create_hitbox':
                        hb = ev.get('data', {}).get('hitbox', {})
                        hb_id = int(hb.get('hitbox_id', 0))
                        active_by_hitbox[hb_id] = _env_damage_int(float(hb.get('damage', 0.0)))
                    elif kind == 'set_hitbox_damage':
                        hb_id = int(ev.get('data', {}).get('idx', 0))
                        if hb_id in active_by_hitbox:
                            active_by_hitbox[hb_id] = _env_damage_int(float(ev.get('data', {}).get('damage', 0.0)))
                    elif kind == 'remove_hitbox':
                        active_by_hitbox.pop(int(ev.get('data', {}).get('idx', 0)), None)
                    elif kind == 'clear_hitboxes':
                        active_by_hitbox.clear()
                frame_damage[frame] = max(active_by_hitbox.values(), default=0)
            active_int_damage_by_anim[submotion_id] = frame_damage
        char_active_shield_hit_int_damage[int(cid)] = active_int_damage_by_anim
    max_anim_idx = max((max(v.keys(), default=0) for v in char_active_shield_hit_int_damage.values()), default=0)
    max_active_frame = 0
    for by_anim in char_active_shield_hit_int_damage.values():
        for by_frame in by_anim.values():
            if by_frame:
                max_active_frame = max(max_active_frame, max(by_frame.keys()))
    active_shield_hit_lut = np.zeros((256, max_anim_idx + 1, max_active_frame + 1), dtype=np.uint16)
    for cid, by_anim in char_active_shield_hit_int_damage.items():
        for anim_idx, by_frame in by_anim.items():
            for frame, dmg in by_frame.items():
                active_shield_hit_lut[int(cid) & 255, int(anim_idx), int(frame)] = np.uint16(int(dmg))
    char_gr_friction_lut = np.zeros(256, dtype=np.float32)
    for cid, value in char_gr_friction.items():
        char_gr_friction_lut[int(cid) & 255] = np.float32(float(value))
    rebound_numerator_lut = np.zeros(256, dtype=np.float32)
    for cid, numerator in char_rebound_anim_numerator_frames.items():
        rebound_numerator_lut[int(cid) & 255] = np.float32(float(numerator))
    return _ManifestPreprocessTables(manifest_chars=manifest_chars, char_landing_air_lag_frames=char_landing_air_lag_frames, char_fallspecial_origin_lag=char_fallspecial_origin_lag, char_fallspecial_origin_allow_interrupt=char_fallspecial_origin_allow_interrupt, char_walk_divisors=char_walk_divisors, char_walk_max=char_walk_max, char_run_scaling=char_run_scaling, char_gr_friction=char_gr_friction, char_gr_friction_lut=char_gr_friction_lut, char_rebound_anim_numerator_frames=char_rebound_anim_numerator_frames, rebound_numerator_lut=rebound_numerator_lut, char_can_walljump=char_can_walljump, char_walljump_setup_x_delta_threshold=char_walljump_setup_x_delta_threshold, active_shield_hit_lut=active_shield_hit_lut, sheik_char_id=sheik_char_id, zelda_char_id=zelda_char_id, sheik_vanish_travel_frames=sheik_vanish_travel_frames, sheik_vanish_ground_contact_min_frames=sheik_vanish_ground_contact_min_frames, zelda_farore_travel_frames=zelda_farore_travel_frames, zelda_farore_ground_contact_min_frames=zelda_farore_ground_contact_min_frames, sheik_chain_release_min_frames=sheik_chain_release_min_frames)

@functools.lru_cache(maxsize=None)
def _load_u8_character_attr_lut_cached(data_root_text: str, key: str) -> np.ndarray:
    """Load a per-character uint8 attr from data/characters/<char>.json.

    Source owner: runtime-required character attrs are registry/manifest scoped. Keeping
    preprocessing LUTs registry-backed prevents newly added characters from silently inheriting
    zero-valued hidden lanes when the runtime already has extracted character data.
    """
    data_root = Path(data_root_text)
    lut = np.zeros(256, dtype=np.uint8)
    for char_id, name in manifest_registry_chars(data_root):
        attrs = _load_character_attrs(data_root, name)
        if key not in attrs:
            raise ValueError(f'missing required character attr {key!r} for {name}')
        value = int(attrs[key])
        if value < 0 or value > 255:
            raise ValueError(f'character attr {key!r} for {name} out of uint8 range: {value}')
        lut[np.uint8(char_id)] = np.uint8(value)
    return lut

def _load_u8_character_attr_lut(data_root: Path, key: str) -> np.ndarray:
    return _load_u8_character_attr_lut_cached(_path_cache_key(data_root), str(key))

@functools.lru_cache(maxsize=None)
def _load_f32_character_attr_lut_cached(data_root_text: str, key: str) -> np.ndarray:
    """Load a per-character float attr from data/characters/<char>.json."""
    data_root = Path(data_root_text)
    lut = np.zeros(256, dtype=np.float32)
    for char_id, name in manifest_registry_chars(data_root):
        attrs = _load_character_attrs(data_root, name)
        if key not in attrs:
            raise ValueError(f'missing required character attr {key!r} for {name}')
        lut[np.uint8(char_id)] = np.float32(float(attrs[key]))
    return lut

def _load_f32_character_attr_lut(data_root: Path, key: str) -> np.ndarray:
    return _load_f32_character_attr_lut_cached(_path_cache_key(data_root), str(key))


@functools.lru_cache(maxsize=None)
def _multijump_ladder_lut_cached(data_root_text: str) -> tuple[np.ndarray, np.ndarray]:
    """Per-char (first_action, rung_count) for the ftPr_MS_JumpAerialF1..F5 ladder block.

    Multi-jump chars (puff; kirby later) replace the common JumpAerialF/B pair with five
    char-range rung actions starting at the shared char action base 341. Presence keys on the
    extracted fp->x2D0 stats block (puff_mjump_turn_frames) like the runtime has_multijump gate.
    refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_Count == 341)
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D730C
    """
    data_root = Path(data_root_text)
    first = np.zeros(256, dtype=np.uint16)
    count = np.zeros(256, dtype=np.uint8)
    for char_id, name in manifest_registry_chars(data_root):
        attrs = _load_character_attrs(data_root, name)
        if 'puff_mjump_turn_frames' in attrs:
            first[np.uint8(char_id)] = np.uint16(341)
            count[np.uint8(char_id)] = np.uint8(5)
    return first, count


def _multijump_ladder_lut(data_root: Path) -> tuple[np.ndarray, np.ndarray]:
    return _multijump_ladder_lut_cached(_path_cache_key(data_root))

def _derive_common_fall_blend_seed(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, speed_air_x_self_f32: np.ndarray, facing_dir_f32: np.ndarray, air_drift_max_by_char: np.ndarray, threshold: float, lerp: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Derive prefix-causal mv.co.{fall,fallaerial,fallspecial}.x4/smid seed lanes."""
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_common_fall_blend_seed is required; run `make build`') from exc
    return msl_binding.derive_common_fall_blend_seed(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(speed_air_x_self_f32, dtype=np.float32), _ascontiguousarray(facing_dir_f32, dtype=np.float32), _ascontiguousarray(air_drift_max_by_char, dtype=np.float32), float(threshold), float(lerp))

def _derive_sheik_needle_seed_lanes(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, chain_article_present_u8: np.ndarray, sheik_internal_id: int | None) -> tuple[np.ndarray, np.ndarray]:
    """Derive prefix-causal Sheik Needle `fv.sk.x0` count and End timer seed lanes.

    `chain_article_present_u8` is the prefix-visible per-player flag for a live Sheik Chain article
    (source `fv.sk.x8 != NULL`); the Side-B `ftSk_Init_80110198` take-damage callback that clears
    `fv.sk.x0` is installed during SpecialN, or during SpecialS only while that article exists.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_sheik_needle_seed_lanes is required; run `make build`') from exc
    sheik_id = -1 if sheik_internal_id is None else int(sheik_internal_id)
    return msl_binding.derive_sheik_needle_seed_lanes(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(action_frame_i16, dtype=np.int16), _ascontiguousarray(chain_article_present_u8, dtype=np.uint8), sheik_id)

def _derive_sheik_chain_seed_lanes(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, buttons_u16: np.ndarray, hitlag_u16: np.ndarray, sheik_internal_id: int | None, b_mask: int, release_min_frames: int) -> tuple[np.ndarray, np.ndarray]:
    """Derive prefix-causal Sheik Chain `mv.sk.specials.x0/x4` seed lanes."""
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_sheik_chain_seed_lanes is required; run `make build`') from exc
    sheik_id = -1 if sheik_internal_id is None else int(sheik_internal_id)
    return msl_binding.derive_sheik_chain_seed_lanes(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(buttons_u16, dtype=np.uint16), _ascontiguousarray(hitlag_u16, dtype=np.uint16), sheik_id, int(b_mask), int(release_min_frames))

def _derive_zelda_twin_state_flags_2218(*, char_id_u8: np.ndarray, state_flags_u8: np.ndarray, zelda_internal_id: int | None) -> np.ndarray:
    """Derive the hidden Zelda twin fp+0x2218 byte used by Sheik/Zelda transform handoff."""
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_zelda_twin_state_flags_2218 is required; run `make build`') from exc
    zelda_id = -1 if zelda_internal_id is None else int(zelda_internal_id)
    return msl_binding.derive_zelda_twin_state_flags_2218(_ascontiguousarray(char_id_u8, dtype=np.uint8), _ascontiguousarray(state_flags_u8, dtype=np.uint8), zelda_id)

@dataclass(frozen=True)
class PortStatic:
    team_id: int
    char_id: int
    handicap: int

def _to_numpy(arr) -> np.ndarray:
    x = arr.to_numpy(zero_copy_only=False)
    if isinstance(x, np.ma.MaskedArray):
        x = x.filled(0)
    if isinstance(x, np.ndarray) and x.dtype.kind == 'f':
        x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x
