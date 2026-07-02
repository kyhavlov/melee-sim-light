from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from tools.slippi.validation_buffer_builder import ValidationReplayBuffers, build_validation_buffers_from_slp


def replay_path(path: str | Path) -> Path:
    p = Path(path)
    if "datasets" in p.parts or p.suffix not in {".slp", ".slpz"}:
        raise ValueError(f"tests must use explicit replay paths, got {p}")
    return p


@dataclass(frozen=True)
class ReplayBufferRows:
    seed_t: np.ndarray
    prev_input_t: np.ndarray
    input_t: np.ndarray
    ref_t1: np.ndarray

    @property
    def shape(self) -> tuple[int, ...]:
        return self.seed_t.shape

    def __len__(self) -> int:
        return int(self.seed_t.shape[0])

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]

    def __getitem__(self, key: Any):
        if isinstance(key, str):
            if key == "seed_t":
                return self.seed_t
            if key == "prev_input_t":
                return self.prev_input_t
            if key == "input_t":
                return self.input_t
            if key == "ref_t1":
                return self.ref_t1
            raise KeyError(key)
        return ReplayBufferRows(
            seed_t=self.seed_t[key],
            prev_input_t=self.prev_input_t[key],
            input_t=self.input_t[key],
            ref_t1=self.ref_t1[key],
        )

    def __setitem__(self, key: str, value: Any) -> None:
        if key == "seed_t":
            self.seed_t[...] = value
        elif key == "prev_input_t":
            self.prev_input_t[...] = value
        elif key == "input_t":
            self.input_t[...] = value
        elif key == "ref_t1":
            self.ref_t1[...] = value
        else:
            raise KeyError(key)

    def copy(self) -> "ReplayBufferRows":
        return ReplayBufferRows(
            seed_t=self.seed_t.copy(),
            prev_input_t=self.prev_input_t.copy(),
            input_t=self.input_t.copy(),
            ref_t1=self.ref_t1.copy(),
        )


@dataclass(frozen=True)
class LoadedReplayBuffers:
    buffers: ValidationReplayBuffers
    rows: ReplayBufferRows
    num_players: int


@dataclass(frozen=True)
class ReplayBufferByteViews:
    seed_t: np.ndarray
    prev_input_t: np.ndarray
    input_t: np.ndarray
    ref_t1: np.ndarray


def replay_buffer_byte_views(loaded: LoadedReplayBuffers) -> ReplayBufferByteViews:
    return ReplayBufferByteViews(
        seed_t=loaded.buffers.seed_u8(),
        prev_input_t=loaded.buffers.prev_input_u8(),
        input_t=loaded.buffers.input_u8(),
        ref_t1=loaded.buffers.ref_u8(),
    )


def replay_buffer_row_bytes(
    loaded: LoadedReplayBuffers, group: str, record: int, stride: int | None = None
) -> np.ndarray:
    views = replay_buffer_byte_views(loaded)
    try:
        arr = getattr(views, group)
    except AttributeError as exc:
        raise KeyError(group) from exc
    width = int(arr.shape[1]) if stride is None else int(stride)
    return np.array(
        arr[int(record) : int(record) + 1, :width],
        dtype=np.uint8,
        order="C",
        copy=True,
    )


def _loaded(buffers: ValidationReplayBuffers) -> LoadedReplayBuffers:
    return LoadedReplayBuffers(
        buffers=buffers,
        rows=ReplayBufferRows(
            seed_t=buffers.seed_t,
            prev_input_t=buffers.prev_input_t,
            input_t=buffers.input_t,
            ref_t1=buffers.ref_t1,
        ),
        num_players=int(buffers.num_players),
    )


def load_replay_buffers(
    path: str | Path | None = None,
    *,
    slp_path: str | Path | None = None,
    ports: list[int] | None = None,
    ucf_enabled: bool = True,
    ucf_cardinals_1_0_enabled: bool = False,
) -> LoadedReplayBuffers:
    if path is None:
        if slp_path is None:
            raise TypeError("path or slp_path is required")
        path = slp_path
    replay = replay_path(path)
    return _loaded(
        build_validation_buffers_from_slp(
            slp_path=str(replay),
            ports=ports,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
    )


def load_replay_buffer_window(
    path: str | Path, start: int, stop: int, *, ports: list[int] | None = None
) -> LoadedReplayBuffers:
    loaded = load_replay_buffers(path, ports=ports)
    buffers = loaded.buffers
    rows = loaded.rows[int(start) : int(stop)]
    return LoadedReplayBuffers(buffers=buffers, rows=rows, num_players=loaded.num_players)
