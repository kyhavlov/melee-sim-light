from __future__ import annotations

import json
import os
from pathlib import Path
from types import TracebackType
from typing import Self, Sequence

import numpy as np

from . import _native
from .buffers import Buffers
from .config import Character, MatchConfig, PlayerConfig, Stage
from .raw_data import resolve_data_root


_DEFAULT_DATA_DIR = "data"
_DATA_MANIFEST = "manifest.json"

# Char-independent artifacts every data root must carry.
_REQUIRED_COMMON_DATA_FILES = (
    "stages/bin/grnla.bin",
    "common/ft_common_data.json",
    "items/lasers.bin",
    "items/item_common.json",
    "items/articles/fox_falco.bin",
    "stage_items/yoshi_shyguy.bin",
    "stage_items/dream_whispy.bin",
)

# Per-character artifact templates. The character list comes from the data root's own
# manifest ("chars", written by build_data from the registry), so a root built for
# fox/falco/marth preflights ALL three - hardcoding fox/falco here let a marth-less root
# pass Python preflight and fail later as a generic native init error.
_REQUIRED_CHAR_DATA_FILE_TEMPLATES = (
    "characters/{char}.json",
    "special_msids/{char}.json",
    "moves/{char}.json",
    "attack_id/move_id/{char}.bin",
    "motion_state/owners/{char}.bin",
    "anims/{char}.bin",
    "hurtcaps/{char}.bin",
    "scripts/{char}.bin",
    "hitboxes/{char}.bin",
    "ecb/{char}_bottom.bin",
    "ecb/{char}_extents.bin",
)

_FALLBACK_DATA_CHARS = ("fox", "falco")


def _data_manifest_chars(data_dir: Path) -> tuple[str, ...]:
    try:
        payload = json.loads((data_dir / _DATA_MANIFEST).read_text(encoding="utf-8"))
        chars = payload.get("chars")
        if isinstance(chars, list) and chars and all(isinstance(c, str) for c in chars):
            return tuple(chars)
    except (OSError, json.JSONDecodeError):
        pass
    return _FALLBACK_DATA_CHARS


class EnvBatch:
    __slots__ = (
        "batch_size",
        "length",
        "num_players",
        "data_dir",
        "t",
        "_handle",
        "_bound",
        "_closed",
    )

    def __init__(
        self,
        batch_size: int,
        length: int = 256,
        num_players: int = 2,
        *,
        data_dir: str | os.PathLike[str] | None = None,
        observation: str = "native",
        action_format: str = "controller",
        obs_dim: int = 0,
        ucf_enabled: bool = True,
        ucf_cardinals_1_0_enabled: bool = False,
    ) -> None:
        self.batch_size = int(batch_size)
        self.length = int(length)
        self.num_players = int(num_players)
        self.data_dir = _resolve_data_dir(data_dir)
        self.t = 0
        if self.data_dir is not None:
            data_path = Path(self.data_dir)
            _check_data_dir(data_path)
            _check_data_manifest(data_path)
            # Hand the validated root to the native loaders. This is process-global by the
            # C loaders' design (they latch on first init); set it only now - after both
            # preflights passed - and roll it back if construction fails so an aborted
            # EnvBatch cannot leave a stale override behind.
            _native.set_data_dir(self.data_dir)
        try:
            self._handle = _native.init(
                batch_size=self.batch_size,
                num_players=self.num_players,
                ucf_enabled=int(ucf_enabled),
                ucf_cardinals_1_0_enabled=int(ucf_cardinals_1_0_enabled),
            )
        except BaseException:
            if self.data_dir is not None:
                _native.clear_data_dir()
            raise
        self._bound: Buffers | None = None
        self._closed = False
        self.bind(
            self.allocate_buffers(
                observation=observation,
                action_format=action_format,
                obs_dim=obs_dim,
            )
        )

    def close(self) -> None:
        if not self._closed:
            _native.destroy(self._handle)
            self._closed = True

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @property
    def buffers(self) -> Buffers:
        if self._bound is None:
            raise RuntimeError("no buffers are bound")
        return self._bound

    @property
    def match_config_view(self) -> np.ndarray:
        return self.buffers.match_config_view

    @property
    def action_view(self) -> np.ndarray:
        return self.buffers.action_view

    @property
    def raw_action_view(self) -> np.ndarray:
        return self.buffers.raw_action_view

    @property
    def controller_action_view(self) -> np.ndarray:
        return self.buffers.controller_action_view

    @property
    def compare_view(self) -> np.ndarray:
        return self.buffers.compare_view

    @property
    def gamestate_view(self) -> np.ndarray:
        return self.buffers.gamestate_view

    @property
    def terminal_view(self) -> np.ndarray:
        return self.buffers.terminal_view

    @property
    def current_frame(self) -> np.ndarray:
        self._check_bound()
        if self.t < 0 or self.t > self.length:
            raise RuntimeError("current frame is outside the gamestate buffer")
        return self.gamestate_view[self.t]

    @property
    def current_action_frame(self) -> np.ndarray:
        self._check_t_in_length()
        return self.action_view[self.t]

    @property
    def current_reset_mask(self) -> np.ndarray:
        self._check_t_in_length()
        return self.buffers.reset_mask[self.t]

    def done_at(self, t: int) -> np.ndarray:
        return self.buffers.done[self._checked_step_index(t)]

    def terminal_at(self, t: int) -> np.ndarray:
        return self.terminal_view[self._checked_step_index(t)]

    def allocate_buffers(
        self,
        *,
        observation: str = "native",
        action_format: str = "controller",
        obs_dim: int = 0,
    ) -> Buffers:
        return Buffers.empty(
            length=self.length,
            batch_size=self.batch_size,
            num_players=self.num_players,
            observation=observation,
            action_format=action_format,
            obs_dim=obs_dim,
        )

    def configure_match(
        self,
        buffers: Buffers | None = None,
        config: MatchConfig | None = None,
        *,
        stage: int | Stage | None = None,
        players: Sequence[PlayerConfig] | None = None,
        frame_id: int | None = None,
        frame_pre_random_seed: int | Sequence[int] | np.ndarray | None = None,
        stock_count: int | None = None,
        match_damage_ratio: float | None = None,
        is_teams: bool | None = None,
        camera_mode: int | None = None,
        env_ids: Sequence[int] | np.ndarray | None = None,
    ) -> None:
        buffers = self.buffers if buffers is None else buffers
        cfg_obj = config or MatchConfig()
        if stage is not None:
            cfg_obj = MatchConfig(
                stage=stage,
                players=cfg_obj.players,
                frame_id=cfg_obj.frame_id,
                frame_pre_random_seed=cfg_obj.frame_pre_random_seed,
                stock_count=cfg_obj.stock_count,
                match_damage_ratio=cfg_obj.match_damage_ratio,
                is_teams=cfg_obj.is_teams,
                camera_mode=cfg_obj.camera_mode,
            )
        if players is not None:
            cfg_obj = MatchConfig(
                stage=cfg_obj.stage,
                players=tuple(players),
                frame_id=cfg_obj.frame_id,
                frame_pre_random_seed=cfg_obj.frame_pre_random_seed,
                stock_count=cfg_obj.stock_count,
                match_damage_ratio=cfg_obj.match_damage_ratio,
                is_teams=cfg_obj.is_teams,
                camera_mode=cfg_obj.camera_mode,
            )
        if frame_id is not None:
            cfg_obj = _replace_config(cfg_obj, frame_id=int(frame_id))
        if frame_pre_random_seed is not None:
            seeds = np.asarray(frame_pre_random_seed, dtype=np.uint32)
            if seeds.ndim == 0:
                cfg_obj = _replace_config(cfg_obj, frame_pre_random_seed=int(seeds))
            else:
                self.configure_matches(
                    [_replace_config(cfg_obj, frame_pre_random_seed=int(seed)) for seed in seeds],
                    buffers=buffers,
                    env_ids=env_ids,
                    stock_count=stock_count,
                    match_damage_ratio=match_damage_ratio,
                    is_teams=is_teams,
                    camera_mode=camera_mode,
                )
                return
        if stock_count is not None:
            cfg_obj = _replace_config(cfg_obj, stock_count=int(stock_count))
        if match_damage_ratio is not None:
            cfg_obj = _replace_config(cfg_obj, match_damage_ratio=float(match_damage_ratio))
        if is_teams is not None:
            cfg_obj = _replace_config(cfg_obj, is_teams=bool(is_teams))
        if camera_mode is not None:
            cfg_obj = _replace_config(cfg_obj, camera_mode=int(camera_mode))

        ids = _env_ids(buffers, env_ids)
        self.configure_matches([cfg_obj] * len(ids), buffers=buffers, env_ids=ids)

    def configure_matches(
        self,
        configs: Sequence[MatchConfig],
        *,
        buffers: Buffers | None = None,
        env_ids: Sequence[int] | np.ndarray | None = None,
        stock_count: int | None = None,
        match_damage_ratio: float | None = None,
        is_teams: bool | None = None,
        camera_mode: int | None = None,
    ) -> None:
        self._check_open()
        buffers = self.buffers if buffers is None else buffers
        self._check_buffers_compatible(buffers)
        ids = _env_ids(buffers, env_ids)
        if len(configs) != len(ids):
            raise ValueError("configs length must match selected env count")

        match = buffers.match_config_view
        for lane, config in zip(ids, configs, strict=True):
            cfg = config
            if stock_count is not None:
                cfg = _replace_config(cfg, stock_count=int(stock_count))
            if match_damage_ratio is not None:
                cfg = _replace_config(cfg, match_damage_ratio=float(match_damage_ratio))
            if is_teams is not None:
                cfg = _replace_config(cfg, is_teams=bool(is_teams))
            if camera_mode is not None:
                cfg = _replace_config(cfg, camera_mode=int(camera_mode))
            _write_match_config_row(match[lane], cfg, lane=lane, num_players=self.num_players)

    def bind(self, buffers: Buffers) -> None:
        self._check_open()
        self._check_buffers_compatible(buffers)
        if buffers.length != self.length:
            raise ValueError("buffers.length must match EnvBatch.length")
        _native.bind_sequence_buffers(
            self._handle,
            buffers.match_config,
            buffers.action,
            buffers.compare,
            buffers.viewpoint,
            buffers.gamestate,
            buffers.terminal,
            buffers.done,
            buffers.reset_mask,
            buffers.action_format,
        )
        self._bound = buffers
        self.t = 0

    def unbind(self) -> None:
        self._check_open()
        _native.unbind_buffers(self._handle)
        self._bound = None

    def reset_all(self) -> None:
        self._check_bound()
        _native.init_match_sequence_bound(self._handle)
        self.t = 0

    def reset_previous_input(self) -> None:
        self._check_open()
        _native.reset_prev_input(self._handle)

    def reset_cursor(self) -> None:
        self._check_bound()
        self.t = 0

    def reset_masked(self, *, write_initial_observation: bool = True) -> None:
        self._check_bound()
        self._check_t_in_length()
        _native.reset_sequence_masked(self._handle, self.t, int(write_initial_observation))

    def set_previous_input(self, t: int = 0) -> None:
        self._check_bound()
        _native.set_prev_input_from_sequence(self._handle, int(t))

    def step(
        self,
        *,
        write_outputs: bool = True,
        write_compare: bool = False,
        max_frame_id: int = -1,
    ) -> None:
        self._step_at(
            self.t,
            write_outputs=write_outputs,
            write_compare=write_compare,
            max_frame_id=max_frame_id,
        )
        self.t += 1

    def _step_at(
        self,
        t: int,
        *,
        write_outputs: bool = True,
        write_compare: bool = False,
        max_frame_id: int = -1,
        update_cursor: bool = False,
    ) -> None:
        self._check_bound()
        if t < 0 or t >= self.length:
            raise RuntimeError("buffer length exhausted")
        _native.step_sequence(
            self._handle,
            int(t),
            int(write_outputs),
            int(write_compare),
            int(max_frame_id),
        )
        if update_cursor:
            self.t = int(t) + 1

    def write_compare(self) -> None:
        self._check_bound()
        _native.write_compare_bound(self._handle)

    def _check_open(self) -> None:
        if self._closed:
            raise RuntimeError("EnvBatch is closed")

    def _check_bound(self) -> None:
        self._check_open()
        if self._bound is None:
            raise RuntimeError("no buffers are bound")

    def _check_t_in_length(self) -> None:
        if self.t < 0 or self.t >= self.length:
            raise RuntimeError("buffer length exhausted; call reset_cursor(), reset_all(), or bind new buffers")

    def _checked_step_index(self, t: int) -> int:
        t = int(t)
        if t < 0 or t >= self.length:
            raise RuntimeError("step index is outside the step buffer")
        return t

    def _check_buffers_compatible(self, buffers: Buffers) -> None:
        if buffers.batch_size < self.batch_size:
            raise ValueError("buffers.batch_size is smaller than EnvBatch.batch_size")
        if buffers.num_players != self.num_players:
            raise ValueError("buffers.num_players must match EnvBatch.num_players")


def _replace_config(config: MatchConfig, **kwargs) -> MatchConfig:
    values = {
        "stage": config.stage,
        "players": config.players,
        "frame_id": config.frame_id,
        "frame_pre_random_seed": config.frame_pre_random_seed,
        "stock_count": config.stock_count,
        "match_damage_ratio": config.match_damage_ratio,
        "is_teams": config.is_teams,
        "camera_mode": config.camera_mode,
    }
    values.update(kwargs)
    return MatchConfig(**values)


def _env_ids(buffers: Buffers, env_ids: Sequence[int] | np.ndarray | None) -> np.ndarray:
    if env_ids is None:
        return np.arange(buffers.batch_size, dtype=np.int64)
    ids = np.asarray(env_ids, dtype=np.int64)
    if ids.ndim != 1:
        raise ValueError("env_ids must be a 1D sequence")
    if np.any(ids < 0) or np.any(ids >= buffers.batch_size):
        raise ValueError("env_ids contains an out-of-range lane")
    return ids


def _write_match_config_row(row: np.void, config: MatchConfig, *, lane: int, num_players: int) -> None:
    players = _default_players(num_players) if config.players is None else tuple(config.players)
    if len(players) != num_players:
        raise ValueError(f"players must contain exactly {num_players} entries")

    for name in row.dtype.names or ():
        row[name] = 0
    row["stage_id"] = int(config.stage)
    row["frame_id"] = int(config.frame_id)
    row["frame_pre_random_seed"] = lane if config.frame_pre_random_seed is None else int(config.frame_pre_random_seed)
    row["match_damage_ratio"] = float(config.match_damage_ratio)
    row["num_players"] = num_players
    row["is_teams"] = int(bool(config.is_teams))
    row["stock_count"] = int(config.stock_count)
    row["camera_mode"] = int(config.camera_mode)

    player_view = row["players"]
    for i, player in enumerate(players):
        player_view["char_id"][i] = _u8("character", int(player.character))
        team_id = _default_team_id(i, num_players, bool(config.is_teams)) if player.team_id is None else player.team_id
        facing = (1 if i == 0 else 0) if player.facing is None else player.facing
        player_view["team_id"][i] = _u8("team_id", int(team_id))
        player_view["facing"][i] = _u8("facing", int(facing))


def _default_players(num_players: int) -> tuple[PlayerConfig, ...]:
    if num_players == 2:
        return (
            PlayerConfig(character=Character.FOX),
            PlayerConfig(character=Character.FALCO),
        )
    raise ValueError("players must be provided when num_players is not 2")


def _default_team_id(player_index: int, num_players: int, is_teams: bool) -> int:
    if not is_teams:
        return player_index
    return 0 if player_index < (num_players + 1) // 2 else 1


def _u8(name: str, value: int) -> int:
    if value < 0 or value > 255:
        raise ValueError(f"{name} must fit in uint8")
    return value


def _resolve_data_dir(data_dir: str | os.PathLike[str] | None) -> str | None:
    # Pure resolution - NO side effects. The chosen root reaches the native loaders via
    # _native.set_data_dir, but that happens in EnvBatch.__init__ only AFTER the data-dir
    # and manifest preflights pass: resolving a path (this helper, also called directly by
    # tests) must never poison the process-global override for later native init calls.
    # The MSL_DATA_DIR env var is read-only input; os.environ is never written.
    if data_dir is not None:
        return str(resolve_data_root(data_dir))

    msl_data = os.environ.get("MSL_DATA_DIR")
    if msl_data:
        return str(resolve_data_root(msl_data))

    default_data = resolve_data_root(default=_DEFAULT_DATA_DIR)
    if default_data.exists():
        return str(default_data)

    raise FileNotFoundError(_missing_data_dir_message(default_data))


def _missing_data_dir_message(default_data: Path) -> str:
    return "\n".join(
        [
            "melee_sim data directory not found.",
            f"  checked default data root: {default_data}",
            "",
            "Run:",
            "  python -m melee_sim.extract_data --iso /path/to/SSBM.iso",
            "",
            "Or set the data root explicitly:",
            "  MSL_DATA_DIR=/path/to/data python your_script.py",
            "  env = melee_sim.EnvBatch(..., data_dir='/path/to/data')",
        ]
    )


def _check_data_dir(data_dir: Path) -> None:
    required = list(_REQUIRED_COMMON_DATA_FILES)
    for char in _data_manifest_chars(data_dir):
        required.extend(t.format(char=char) for t in _REQUIRED_CHAR_DATA_FILE_TEMPLATES)
    missing = [rel for rel in required if not (data_dir / rel).exists()]
    if missing:
        preview = "\n".join(f"  - {rel}" for rel in missing[:8])
        extra = "" if len(missing) <= 8 else f"\n  ... and {len(missing) - 8} more"
        raise FileNotFoundError(
            f"missing melee_sim data files under {data_dir}:\n{preview}{extra}\n"
            "Run `python -m melee_sim.extract_data --iso /path/to/SSBM.iso`, "
            "set MSL_DATA_DIR, or pass EnvBatch(..., data_dir=...)."
        )


def _check_data_manifest(data_dir: Path) -> None:
    manifest_path = data_dir / _DATA_MANIFEST
    if not manifest_path.exists():
        return
    try:
        payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"invalid melee_sim data manifest: {manifest_path}: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("magic") != "MSLDATA1" or payload.get("version") != 1:
        raise RuntimeError(f"unsupported melee_sim data manifest header: {manifest_path}")
    schemas = payload.get("schemas")
    if not isinstance(schemas, dict):
        raise RuntimeError(f"missing schemas object in melee_sim data manifest: {manifest_path}")
    try:
        runtime_schemas = _native.data_schema_versions()
    except AttributeError as exc:
        raise RuntimeError(
            _data_schema_error_message(
                data_dir=data_dir,
                mismatches=[],
                extra=(
                    "The data root has a schema manifest, but the loaded native extension is too "
                    "old to report its expected data schemas."
                ),
            )
        ) from exc

    mismatches: list[tuple[str, int | None, int | None]] = []
    if isinstance(runtime_schemas, dict):
        # A manifest that simply OMITS a runtime-known schema key must be reported like a
        # mismatch, not silently skipped (omission is how a stale generator hides skew).
        for name in runtime_schemas:
            if isinstance(name, str) and name not in schemas:
                try:
                    runtime_version = int(runtime_schemas[name])
                except (TypeError, ValueError):
                    runtime_version = None
                mismatches.append((name, None, runtime_version))
    for name, data_version_obj in schemas.items():
        if not isinstance(name, str):
            continue
        try:
            data_version = int(data_version_obj)
        except (TypeError, ValueError):
            data_version = None
        runtime_version_obj = runtime_schemas.get(name) if isinstance(runtime_schemas, dict) else None
        try:
            runtime_version = int(runtime_version_obj)
        except (TypeError, ValueError):
            runtime_version = None
        if data_version != runtime_version:
            mismatches.append((name, data_version, runtime_version))
    if mismatches:
        raise RuntimeError(_data_schema_error_message(data_dir=data_dir, mismatches=mismatches))


def _data_schema_error_message(
    *,
    data_dir: Path,
    mismatches: list[tuple[str, int | None, int | None]],
    extra: str | None = None,
) -> str:
    lines = [
        "melee-sim-light data/runtime schema mismatch.",
        f"  data root: {data_dir}",
        f"  native extension: {getattr(_native, '__file__', '<unknown>')}",
    ]
    if extra:
        lines.append(f"  detail: {extra}")
    if mismatches:
        lines.append("  mismatched schemas:")
        for name, data_version, runtime_version in mismatches:
            lines.append(f"    - {name}: data={data_version} runtime={runtime_version}")
    lines.extend(
        [
            "",
            "This usually means extracted data was generated by one melee-sim-light checkout, but this",
            "Python environment loaded a stale native extension from another install/build.",
            "",
            "Fix it in the Python environment running this process:",
            "  python -m pip install --force-reinstall --no-cache-dir /path/to/melee-sim-light",
            "",
            "If Python imports melee_sim from a source checkout, an in-place rebuild there is also valid:",
            "  cd /path/to/melee-sim-light && make build",
            "",
            "Then regenerate data or point MSL_DATA_DIR at a data root from the same checkout:",
            "  python -m melee_sim.extract_data --iso /path/to/SSBM.iso --force",
        ]
    )
    return "\n".join(lines)
