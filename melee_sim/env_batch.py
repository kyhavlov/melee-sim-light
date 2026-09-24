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


_preloaded = False


def preload_game_data(data_dir: str | os.PathLike[str] | None = None) -> Path:
    """Load the immutable game data once for this process and keep it loaded.

    Every ``EnvBatch`` in the process then shares it instead of loading its
    own copy (roughly 300 MB), and processes forked after this call share the
    pages copy-on-write. Returns the resolved data directory. Calling it again
    with the same directory is a no-op; a different directory is an error,
    since a process holds a single data root.
    """
    global _preloaded
    resolved = _resolve_data_dir(data_dir)
    lib = _native.library()
    _native.check(lib.msl_game_data_acquire(os.fsencode(resolved)), "preload game data")
    if _preloaded:
        # Already holding the preload reference; keep exactly one.
        lib.msl_game_data_release()
    _preloaded = True
    return resolved


def game_data_loaded() -> bool:
    """Whether this process has game data loaded (by a batch or a preload)."""
    return _native.library().msl_game_data_references() > 0


class EnvBatch:
    """One single-threaded batch backed directly by ``src/api.h``."""

    __slots__ = (
        "batch_size",
        "length",
        "num_players",
        "data_dir",
        "t",
        "_handle",
        "_input_storage",
        "_bound",
        "_closed",
        "_reset_mask_scratch",
        "_step_mask_scratch",
        "_pending_reset",
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
        ucf_cardinals_1_0_enabled: bool = True,
    ) -> None:
        self.batch_size = int(batch_size)
        self.length = int(length)
        self.num_players = int(num_players)
        if self.batch_size <= 0:
            raise ValueError("batch_size must be positive")
        if self.num_players not in (2, 3, 4):
            raise ValueError("num_players must be 2, 3, or 4")
        self.data_dir = _resolve_data_dir(data_dir)
        self.t = 0
        self._handle = ctypes.c_void_p()
        self._input_storage = dtypes.raw_buffer(self.batch_size, "input")
        self._bound: Buffers | None = None
        self._closed = False
        self._reset_mask_scratch = np.zeros(self.batch_size, dtype=np.uint8)
        self._step_mask_scratch = np.zeros(self.batch_size, dtype=np.uint8)
        self._pending_reset = np.zeros(self.batch_size, dtype=np.uint8)

        lib = _native.library()
        _native.check(
            lib.msl_batch_create(
                os.fsencode(self.data_dir), self.batch_size, ctypes.byref(self._handle)
            ),
            "create batch",
        )
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
        _native.library().msl_batch_destroy(self._handle)
        self._handle = ctypes.c_void_p()
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
        self._check_bound()
        return self.action_view[0 if self.t >= self.length else self.t]

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
        _native.check(
            _native.library().msl_batch_reset(
                self._handle,
                _native.pointer(self.buffers.match_config),
                None,
                _native.pointer(self.buffers.gamestate[0]),
            ),
            "reset matches",
        )
        self._pending_reset[:] = 0
        self.t = 0

    def reset_matches(self, env_ids: Sequence[int] | np.ndarray) -> None:
        """Reset the selected matches in place.

        Patches only the selected rows of the current gamestate frame; the
        other matches and their published observations are untouched. Valid at
        any cursor position, including the final ring slot.
        """
        self._check_bound()
        ids = np.asarray(env_ids, dtype=np.int64).reshape(-1)
        if ids.size == 0:
            return
        if np.any(ids < 0) or np.any(ids >= self.batch_size):
            raise ValueError("env_ids contains an out-of-range match index")
        mask = self._reset_mask_scratch
        mask[:] = 0
        mask[ids] = 1
        _native.check(
            _native.library().msl_batch_reset(
                self._handle,
                _native.pointer(self.buffers.match_config),
                _native.pointer(mask),
                _native.pointer(self.buffers.gamestate[self.t]),
            ),
            "reset selected matches",
        )
        self._pending_reset[ids] = 0

    def reset_cursor(self) -> None:
        self._check_bound()
        # Carry the current observation to slot 0 so that reads of the current
        # frame (and masked resets, which only overwrite selected rows) see
        # up-to-date data for every match after the cursor wraps.
        if self.t > 0:
            np.copyto(self.buffers.gamestate[0], self.buffers.gamestate[self.t])
        self.t = 0

    def step_and_reset(self, step_mask: np.ndarray | None = None) -> tuple[np.ndarray, np.ndarray]:
        """Reset matches that finished last step, then step the rest.

        Matches flagged done by the previous call are reset in place: their
        queued action for this step is ignored and their published frame is
        the new episode's entry frame. Every other match steps normally, so a
        finishing match publishes its terminal frame once before resetting on
        the following call — episodes keep both their final and initial
        states.

        Returns ``(is_resetting, terminal)`` describing the newly published
        frame: ``is_resetting`` is a copy marking entry-frame rows and
        ``terminal`` is a view of the step's terminal records, whose ``done``
        field marks terminal rows.

        An optional step_mask holds selected lanes without changing reset handling.

        Wraps the cursor (carrying the current observation to slot 0) when the
        ring is exhausted, so callers never index past the per-step buffers;
        write actions through ``current_action_frame``, which is wrap-aware.
        """
        self._check_bound()
        if self.t >= self.length:
            self.reset_cursor()
        fresh = self._pending_reset.astype(np.bool_)
        fresh_ids = np.flatnonzero(fresh)
        if step_mask is not None and step_mask.shape != (self.batch_size,):
            raise ValueError("step_mask must have shape (batch_size,)")
        if fresh_ids.size:
            self.reset_matches(fresh_ids)
            np.logical_not(fresh, out=self._step_mask_scratch)
            if step_mask is not None:
                self._step_mask_scratch &= np.asarray(step_mask, dtype=np.uint8)
            step_mask = self._step_mask_scratch
        step_t = self.t
        self.step(step_mask)
        terminal = self.terminal_view[step_t]
        np.copyto(self._pending_reset, terminal["done"])
        return fresh, terminal

    def step(self, step_mask: np.ndarray | None = None) -> None:
        """Advance the batch one frame and publish observations for all lanes.

        ``step_mask`` selects which matches consume their queued action and
        simulate a frame; unselected matches republish their current state
        unchanged. Bare ``step`` calls do not participate in
        ``step_and_reset``'s pending-reset tracking.
        """
        frame = self._check_step_index(self.t)
        inputs = self._inputs_for_frame(frame)
        if step_mask is None:
            mask_pointer = None
        else:
            if step_mask.shape != (self.batch_size,):
                raise ValueError("step_mask must have shape (batch_size,)")
            mask_pointer = _native.pointer(
                np.ascontiguousarray(step_mask, dtype=np.uint8))
        _native.check(
            _native.library().msl_batch_step_masked(
                self._handle,
                _native.pointer(inputs),
                mask_pointer,
                _native.pointer(self.buffers.gamestate[frame + 1]),
                _native.pointer(self.buffers.terminal[frame]),
            ),
            "step matches",
        )
        self.t += 1

    def observe(self) -> None:
        """Write the current native state without advancing the batch."""
        frame = self._check_step_index(self.t)
        _native.check(
            _native.library().msl_batch_observe(
                self._handle,
                _native.pointer(self.buffers.gamestate[frame]),
                _native.pointer(self.buffers.terminal[frame]),
            ),
            "observe matches",
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
            _native.library().msl_batch_copy(
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
            lib.msl_batch_save_size(
                self._handle, int(match_index), ctypes.byref(size)
            ),
            "measure savestate",
        )
        buffer = ctypes.create_string_buffer(size.value)
        written = ctypes.c_size_t()
        _native.check(
            lib.msl_batch_save(
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
            _native.library().msl_batch_restore(
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
    if num_players in (2, 3, 4):
        players = (
            PlayerConfig(Character.FOX),
            PlayerConfig(Character.FALCO),
        ) * 2
        return players[:num_players]
    raise ValueError("num_players must be 2, 3, or 4")


def _write_match_config(
    row: np.void, config: MatchConfig, lane: int, num_players: int
) -> None:
    players = _default_players(num_players) if config.players is None else config.players
    if len(players) != num_players:
        raise ValueError(f"players must contain exactly {num_players} entries")
    for name in row.dtype.names or ():
        row[name] = 0
    row["stage"] = int(config.stage)
    row["random_seed"] = lane if config.seed is None else int(config.seed)
    row["max_frame"] = int(config.max_frame)
    row["damage_ratio"] = float(config.damage_ratio)
    row["num_players"] = num_players
    row["is_teams"] = bool(config.is_teams)
    row["friendly_fire"] = bool(config.friendly_fire)
    row["stocks"] = int(config.stocks)
    row["viewpoint_player"] = int(config.viewpoint_player)
    row["ucf_cardinals"] = bool(config.ucf_cardinals_1_0_enabled)
    for index, player in enumerate(players):
        team = -1 if player.team_id is None else int(player.team_id)
        facing = 0 if player.facing is None else (1 if player.facing else -1)
        port = -1 if player.controller_port is None else int(player.controller_port)
        if not -1 <= port < 4:
            raise ValueError("controller_port must be in 0..3")
        if not 0 <= player.start_percent <= 100 or int(player.start_percent) != player.start_percent:
            raise ValueError("start_percent must be an integer in 0..100")
        target = row["players"][index]
        target["character"] = int(player.character)
        target["team"] = team
        target["facing"] = facing
        target["controller_port"] = port
        target["costume"] = int(player.costume)
        target["handicap"] = int(player.handicap)
        target["start_percent"] = int(player.start_percent)
