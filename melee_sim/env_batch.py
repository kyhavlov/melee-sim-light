from __future__ import annotations

import ctypes
import os
from dataclasses import replace
from pathlib import Path
from types import TracebackType
from typing import Self, Sequence

import numpy as np

from . import _native, dtypes
from .buffers import Buffers
from .config import Character, MatchConfig, PlayerConfig


class EnvBatch:
    """One single-threaded batch backed directly by ``src/api.h``."""

    __slots__ = (
        "batch_size",
        "length",
        "num_players",
        "data_dir",
        "t",
        "_game_data",
        "_handle",
        "_input_storage",
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
        if self.batch_size <= 0:
            raise ValueError("batch_size must be positive")
        if self.num_players not in (2, 4):
            raise ValueError("num_players must be 2 or 4")
        if not ucf_enabled:
            raise ValueError("the canonical RL runtime currently requires UCF")
        self.data_dir = _resolve_data_dir(data_dir)
        self.t = 0
        self._game_data = ctypes.c_void_p()
        self._handle = ctypes.c_void_p()
        self._input_storage = dtypes.raw_buffer(self.batch_size, "input")
        self._bound: Buffers | None = None
        self._closed = False

        lib = _native.library()
        _native.check(
            lib.msl_core_game_data_create(
                os.fsencode(_raw_data_dir(self.data_dir)), ctypes.byref(self._game_data)
            ),
            "create game data",
        )
        try:
            _native.check(
                lib.msl_core_batch_create(
                    self._game_data, self.batch_size, ctypes.byref(self._handle)
                ),
                "create batch",
            )
        except BaseException:
            lib.msl_core_game_data_destroy(self._game_data)
            self._game_data = ctypes.c_void_p()
            raise
        self.bind(
            self.allocate_buffers(
                observation=observation,
                action_format=action_format,
                obs_dim=obs_dim,
            )
        )
        self.configure_match(
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled
        )

    def close(self) -> None:
        if self._closed:
            return
        lib = _native.library()
        lib.msl_core_batch_destroy(self._handle)
        lib.msl_core_game_data_destroy(self._game_data)
        self._handle = ctypes.c_void_p()
        self._game_data = ctypes.c_void_p()
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
        return self.gamestate_view[self.t]

    @property
    def current_action_frame(self) -> np.ndarray:
        self._check_step_index(self.t)
        return self.action_view[self.t]

    @property
    def current_reset_mask(self) -> np.ndarray:
        self._check_step_index(self.t)
        return self.buffers.reset_mask[self.t]

    def done_at(self, t: int) -> np.ndarray:
        return self.buffers.done[self._check_step_index(t)]

    def terminal_at(self, t: int) -> np.ndarray:
        return self.terminal_view[self._check_step_index(t)]

    def allocate_buffers(
        self,
        *,
        observation: str = "native",
        action_format: str = "controller",
        obs_dim: int = 0,
    ) -> Buffers:
        return Buffers.empty(
            self.length,
            self.batch_size,
            self.num_players,
            observation=observation,
            action_format=action_format,
            obs_dim=obs_dim,
        )

    def bind(self, buffers: Buffers) -> None:
        self._check_open()
        if buffers.length != self.length or buffers.batch_size != self.batch_size:
            raise ValueError("buffer dimensions must match EnvBatch")
        if buffers.num_players != self.num_players:
            raise ValueError("buffers.num_players must match EnvBatch")
        self._bound = buffers
        self.t = 0

    def unbind(self) -> None:
        self._check_open()
        self._bound = None

    def configure_match(
        self,
        buffers: Buffers | None = None,
        config: MatchConfig | None = None,
        *,
        env_ids: Sequence[int] | np.ndarray | None = None,
        **overrides: object,
    ) -> None:
        target = self.buffers if buffers is None else buffers
        value = config or MatchConfig()
        if overrides:
            value = replace(value, **overrides)
        ids = _env_ids(target, env_ids)
        self.configure_matches([value] * len(ids), buffers=target, env_ids=ids)

    def configure_matches(
        self,
        configs: Sequence[MatchConfig],
        *,
        buffers: Buffers | None = None,
        env_ids: Sequence[int] | np.ndarray | None = None,
    ) -> None:
        self._check_open()
        target = self.buffers if buffers is None else buffers
        ids = _env_ids(target, env_ids)
        if len(configs) != len(ids):
            raise ValueError("configs length must match selected environment count")
        rows = target.match_config_view
        for index, config in zip(ids, configs, strict=True):
            _write_match_config(rows[index], config, int(index), self.num_players)

    def reset_all(self) -> None:
        self._check_bound()
        lib = _native.library()
        _native.check(
            lib.msl_core_batch_reset_matches(
                self._handle,
                _native.pointer(self.buffers.match_config),
                self.buffers.match_config.shape[1],
                None,
                0,
            ),
            "reset matches",
        )
        self.t = 0
        self._write_observation(0, None)

    def reset_masked(self, *, write_initial_observation: bool = True) -> None:
        self._check_bound()
        frame = self._check_step_index(self.t)
        mask = self.buffers.reset_mask[frame]
        _native.check(
            _native.library().msl_core_batch_reset_matches(
                self._handle,
                _native.pointer(self.buffers.match_config),
                self.buffers.match_config.shape[1],
                _native.pointer(mask),
                mask.strides[0],
            ),
            "reset selected matches",
        )
        if write_initial_observation:
            self._write_observation(frame, mask)

    def reset_cursor(self) -> None:
        self._check_bound()
        self.t = 0

    def step(
        self,
        *,
        write_outputs: bool = True,
        write_compare: bool = False,
        max_frame_id: int = -1,
    ) -> None:
        frame = self._check_step_index(self.t)
        inputs = self._inputs_for_frame(frame)
        lib = _native.library()
        _native.check(
            lib.msl_core_batch_step_matches(
                self._handle,
                _native.pointer(inputs),
                inputs.strides[0],
                None,
                0,
            ),
            "step matches",
        )
        if write_compare:
            _native.check(
                lib.msl_core_batch_write_state(
                    self._handle,
                    _native.pointer(self.buffers.compare),
                    self.buffers.compare.strides[0],
                    None,
                    0,
                ),
                "write state",
            )
        if write_outputs:
            self._write_observation(frame + 1, None)
            terminal = self.buffers.terminal[frame]
            _native.check(
                lib.msl_core_batch_write_terminal(
                    self._handle,
                    _native.pointer(terminal),
                    terminal.strides[0],
                    int(max_frame_id),
                    None,
                    0,
                ),
                "write terminal",
            )
        self.t += 1

    def write_compare(self) -> None:
        self._check_bound()
        _native.check(
            _native.library().msl_core_batch_write_state(
                self._handle,
                _native.pointer(self.buffers.compare),
                self.buffers.compare.strides[0],
                None,
                0,
            ),
            "write state",
        )

    def copy_matches_from(
        self,
        source: "EnvBatch",
        destination_indices: Sequence[int] | np.ndarray,
        source_indices: Sequence[int] | np.ndarray,
    ) -> None:
        destination = np.ascontiguousarray(destination_indices, dtype=np.uint32)
        sources = np.ascontiguousarray(source_indices, dtype=np.uint32)
        if destination.shape != sources.shape or destination.ndim != 1:
            raise ValueError("source and destination indices must be equal-length vectors")
        _native.check(
            _native.library().msl_core_batch_copy_matches(
                self._handle,
                source._handle,
                _native.pointer(destination),
                _native.pointer(sources),
                destination.size,
            ),
            "copy matches",
        )

    def save(self, match_index: int) -> bytes:
        size = ctypes.c_size_t()
        lib = _native.library()
        _native.check(
            lib.msl_core_batch_match_save_size(
                self._handle, int(match_index), ctypes.byref(size)
            ),
            "measure savestate",
        )
        buffer = ctypes.create_string_buffer(size.value)
        written = ctypes.c_size_t()
        _native.check(
            lib.msl_core_batch_save_match(
                self._handle,
                int(match_index),
                buffer,
                size.value,
                ctypes.byref(written),
            ),
            "save match",
        )
        return buffer.raw[: written.value]

    def restore(self, match_index: int, state: bytes | bytearray | memoryview) -> None:
        view = memoryview(state)
        if not view.contiguous:
            raise ValueError("savestate must be contiguous")
        buffer = (ctypes.c_ubyte * view.nbytes).from_buffer_copy(view)
        _native.check(
            _native.library().msl_core_batch_restore_match(
                self._handle, int(match_index), buffer, view.nbytes
            ),
            "restore match",
        )

    def _inputs_for_frame(self, frame: int) -> np.ndarray:
        action = self.buffers.action[frame]
        if self.buffers.action_format == "raw":
            return action
        result = _native.library().msl_python_controller_inputs(
            _native.pointer(action),
            action.strides[0],
            _native.pointer(self._input_storage),
            self._input_storage.strides[0],
            self.batch_size,
        )
        if result:
            raise ValueError("invalid controller input buffer")
        return self._input_storage

    def _write_observation(self, frame: int, mask: np.ndarray | None) -> None:
        output = self.buffers.gamestate[frame]
        _native.check(
            _native.library().msl_core_batch_write_observation(
                self._handle,
                _native.pointer(self.buffers.viewpoint),
                self.buffers.viewpoint.strides[0],
                _native.pointer(output),
                output.strides[0],
                _native.pointer(mask),
                0 if mask is None else mask.strides[0],
            ),
            "write observation",
        )

    def _check_open(self) -> None:
        if self._closed:
            raise RuntimeError("EnvBatch is closed")

    def _check_bound(self) -> None:
        self._check_open()
        if self._bound is None:
            raise RuntimeError("no buffers are bound")

    def _check_step_index(self, frame: int) -> int:
        self._check_bound()
        frame = int(frame)
        if frame < 0 or frame >= self.length:
            raise RuntimeError("buffer length exhausted")
        return frame


def _resolve_data_dir(data_dir: str | os.PathLike[str] | None) -> Path:
    value = data_dir if data_dir is not None else os.environ.get("MSL_DATA_DIR", "data")
    path = Path(value).expanduser().resolve()
    raw = _raw_data_dir(path)
    if not (raw / "manifest.json").is_file():
        raise FileNotFoundError(
            f"melee_sim data directory is incomplete: {path}\n"
            "Run `python -m melee_sim.extract_data --iso /path/to/SSBM.iso`."
        )
    return path


def _raw_data_dir(path: Path) -> Path:
    nested = path / "raw"
    return nested if nested.is_dir() else path


def _env_ids(buffers: Buffers, env_ids: Sequence[int] | np.ndarray | None) -> np.ndarray:
    if env_ids is None:
        return np.arange(buffers.batch_size, dtype=np.int64)
    ids = np.asarray(env_ids, dtype=np.int64)
    if ids.ndim != 1 or np.any(ids < 0) or np.any(ids >= buffers.batch_size):
        raise ValueError("env_ids must be an in-range one-dimensional sequence")
    return ids


def _default_players(num_players: int) -> tuple[PlayerConfig, ...]:
    if num_players == 2:
        return (
            PlayerConfig(Character.FOX),
            PlayerConfig(Character.FALCO),
        )
    raise ValueError("players must be provided for a four-player match")


def _write_match_config(
    row: np.void, config: MatchConfig, lane: int, num_players: int
) -> None:
    players = _default_players(num_players) if config.players is None else config.players
    if len(players) != num_players:
        raise ValueError(f"players must contain exactly {num_players} entries")
    for name in row.dtype.names or ():
        row[name] = 0
    seed = lane if config.frame_pre_random_seed is None else config.frame_pre_random_seed
    initial_seed = seed if config.initial_random_seed is None else config.initial_random_seed
    row["stage_id"] = int(config.stage)
    row["frame_id"] = int(config.frame_id)
    row["frame_pre_random_seed"] = int(seed)
    row["initial_random_seed"] = int(initial_seed)
    row["match_damage_ratio"] = float(config.match_damage_ratio)
    row["num_players"] = num_players
    row["is_teams"] = bool(config.is_teams)
    row["friendly_fire"] = bool(config.friendly_fire)
    row["stock_count"] = int(config.stock_count)
    row["camera_mode"] = int(config.camera_mode)
    row["ucf_cardinals_1_0_enabled"] = bool(config.ucf_cardinals_1_0_enabled)
    row["ucf_shield_sdi_enabled"] = bool(config.ucf_shield_sdi_enabled)
    row["ucf_sdi_enabled"] = bool(config.ucf_sdi_enabled)
    for index, player in enumerate(players):
        if player.team_id is None:
            team = int(index >= (num_players + 1) // 2) if config.is_teams else index
        else:
            team = int(player.team_id)
        facing = (index == 0) if player.facing is None else bool(player.facing)
        port = 0 if player.controller_port is None else int(player.controller_port) + 1
        if not 0 <= port <= 4:
            raise ValueError("controller_port must be in 0..3")
        target = row["players"][index]
        target["char_id"] = int(player.character)
        target["team_id"] = team
        target["facing_and_port"] = int(facing) | (port << 1)
        target["costume_id"] = int(player.costume)
        target["handicap"] = int(player.handicap)
