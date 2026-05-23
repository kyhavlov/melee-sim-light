from __future__ import annotations

import json
import math
import subprocess
from copy import deepcopy
from dataclasses import dataclass, field
from datetime import datetime, timezone
from functools import lru_cache
from pathlib import Path
from typing import Mapping

import numpy as np

from tools.modelplay.state_adapter import SimFrameState


TRACE_FORMAT = "MSLTRACE1"
SCHEMA_VERSION = 1
SPARSE_DELTA_ENCODING = "sparse-delta-v1"
KEYFRAME_INTERVAL = 60

INPUT_FIELDS = ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
FRAME_FIELDS = ["frame", "randomSeed", "players"]
STAGE_FIELDS = ["randallExists", "randallX", "randallY"]
PLAYER_FIELDS = [
    "charId",
    "actionId",
    "actionFrame",
    "x",
    "y",
    "facing",
    "grounded",
    "percent",
    "shield",
    "stocks",
    "jumps",
    "hitlag",
    "hitstun",
    "hurtbox",
    "reflect",
    "fastfall",
    "shielding",
    "inHitstun",
    "powershield",
    "dead",
]
ITEM_FIELDS = [
    "alive",
    "typeId",
    "state",
    "owner",
    "x",
    "y",
    "vx",
    "vy",
    "facing",
    "damage",
    "timer",
    "spawnId",
    "misc0",
    "misc1",
    "misc2",
]

OP_KEYFRAME = 0
OP_DELTA = 1
REPO_ROOT = Path(__file__).resolve().parents[2]


@lru_cache(maxsize=1)
def _git_build_info() -> dict[str, object] | None:
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"],
            cwd=REPO_ROOT,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=REPO_ROOT,
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    dirty = bool(status.strip())
    return {
        "commit": commit,
        "dirty": dirty,
        "version": f"{commit}+changes" if dirty else commit,
    }


def _default_producer_version() -> str | None:
    info = _git_build_info()
    return None if info is None else str(info["version"])


def _json_value(value: object) -> object:
    if isinstance(value, np.generic):
        value = value.item()
    if isinstance(value, (bool, int, str)) or value is None:
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            return 0
        rounded = round(value, 6)
        if rounded == 0:
            return 0
        return rounded
    return value


def _changed_fields(previous: list[object] | None, current: list[object]) -> list[list[object]]:
    if previous is None:
        return [[idx, value] for idx, value in enumerate(current)]
    changes = []
    for idx, value in enumerate(current):
        if value != previous[idx]:
            changes.append([idx, value])
    return changes


def _buttons_value(controller: object) -> int:
    buttons = getattr(controller, "buttons", None)
    if buttons is None:
        return 0
    value = 0
    if bool(getattr(buttons, "A", False)):
        value |= 0x0100
    if bool(getattr(buttons, "B", False)):
        value |= 0x0200
    if bool(getattr(buttons, "X", False)):
        value |= 0x0400
    if bool(getattr(buttons, "Y", False)):
        value |= 0x0800
    if bool(getattr(buttons, "Z", False)):
        value |= 0x0010
    if bool(getattr(buttons, "L", False)):
        value |= 0x0040
    if bool(getattr(buttons, "R", False)):
        value |= 0x0020
    if bool(getattr(buttons, "D_UP", False)):
        value |= 0x0008
    return value


def _axis_01_to_signed(value: object) -> float:
    return float(np.float32(float(value) * 2.0 - 1.0))


def _input_row(controller: object | None) -> list[object]:
    if controller is None:
        return [0, 0, 0, 0, 0, 0, 0]
    main = getattr(controller, "main_stick", None)
    c_stick = getattr(controller, "c_stick", None)
    return [
        _buttons_value(controller),
        _json_value(_axis_01_to_signed(getattr(main, "x", 0.5))) if main is not None else 0,
        _json_value(_axis_01_to_signed(getattr(main, "y", 0.5))) if main is not None else 0,
        _json_value(_axis_01_to_signed(getattr(c_stick, "x", 0.5))) if c_stick is not None else 0,
        _json_value(_axis_01_to_signed(getattr(c_stick, "y", 0.5))) if c_stick is not None else 0,
        _json_value(float(np.float32(getattr(controller, "shoulder", 0.0)))),
        0,
    ]


def _player_row(state: SimFrameState, idx: int) -> list[object]:
    flags_2218 = int(state.state_flags[idx][0])
    flags_221a = int(state.state_flags[idx][1])
    flags_221b = int(state.state_flags[idx][2])
    flags_221c = int(state.state_flags[idx][3])
    return [
        int(state.char_id[idx]),
        int(state.action_id[idx]),
        _json_value(
            float(
                np.float32(
                    state.anim_frame_f32[idx]
                    if state.anim_frame_f32 is not None and int(state.action_frame[idx]) >= 0
                    else state.action_frame[idx]
                )
            )
        ),
        _json_value(float(np.float32(state.pos_x[idx]))),
        _json_value(float(np.float32(state.pos_y[idx]))),
        -1 if int(state.facing[idx]) == 0 else 1,
        1 if bool(state.on_ground[idx]) else 0,
        _json_value(float(np.float32(state.percent[idx]))),
        _json_value(float(np.float32(state.shield_hp[idx]))),
        int(state.stocks[idx]),
        int(state.jumps_left[idx]),
        int(state.hitlag[idx]),
        int(state.hitstun[idx]),
        int(state.hurtbox_state[idx]),
        1 if flags_2218 & 0x10 else 0,
        1 if flags_221a & 0x08 else 0,
        1 if flags_221b & 0x80 else 0,
        1 if flags_221c & 0x02 else 0,
        1 if flags_221c & 0x20 else 0,
        1 if bool(state.is_dead[idx]) else 0,
    ]


def _item_row(item: np.void) -> list[object]:
    return [
        1,
        int(item["type"]),
        int(item["state"]),
        int(item["owner"]),
        _json_value(float(np.float32(item["pos_x"]))),
        _json_value(float(np.float32(item["pos_y"]))),
        _json_value(float(np.float32(item["vel_x"]))),
        _json_value(float(np.float32(item["vel_y"]))),
        _json_value(float(np.float32(item["direction"]))),
        int(item["damage"]),
        _json_value(float(np.float32(item["timer"]))),
        int(item["spawn_id"]),
        int(item["misc0"]),
        int(item["misc1"]),
        int(item["misc2"]),
    ]


def _stage_row(state: SimFrameState) -> list[object]:
    return [
        1 if bool(state.stage_randall_exists) else 0,
        _json_value(float(np.float32(state.stage_randall_x))),
        _json_value(float(np.float32(state.stage_randall_y))),
    ]


@dataclass
class MslTraceWriter:
    producer_name: str = "melee-sim-light"
    producer_version: str | None = None
    keyframe_interval: int = KEYFRAME_INTERVAL
    metadata: dict = field(default_factory=dict)
    match_start: dict | None = None
    match: dict | None = None
    frame_rows: list[list[object]] = field(default_factory=list)
    stage_rows: list[list[object]] = field(default_factory=list)
    input_streams: list[list[list[object]]] = field(default_factory=list)
    item_rows: list[list[object]] = field(default_factory=list)
    _prev_players: list[list[object]] | None = None
    _prev_seed: int | None = None
    _prev_stage: list[object] | None = None
    _prev_inputs: list[list[object]] = field(default_factory=list)
    _prev_items: dict[int, list[object]] = field(default_factory=dict)
    _frame_count: int = 0

    @property
    def frame_count(self) -> int:
        return self._frame_count

    def add_frame(self, state: SimFrameState, controllers: Mapping[int, object]) -> None:
        if self.match is None:
            self._init_match(state)
        trace_frame = self._frame_count
        is_keyframe = trace_frame == 0 or (trace_frame % self.keyframe_interval) == 0
        player_rows = [_player_row(state, idx) for idx in range(state.num_players)]
        seed = int(state.frame_pre_random_seed)
        if is_keyframe or self._prev_players is None:
            self.frame_rows.append(
                [
                    OP_KEYFRAME,
                    trace_frame,
                    seed,
                    player_rows,
                ]
            )
        else:
            self.frame_rows.append(
                [
                    OP_DELTA,
                    trace_frame,
                    seed if seed != self._prev_seed else None,
                    [
                        _changed_fields(self._prev_players[idx], player_rows[idx])
                        for idx in range(state.num_players)
                    ],
                ]
            )
        self._prev_players = player_rows
        self._prev_seed = seed
        self._append_stage(trace_frame, state, is_keyframe=is_keyframe)
        self._append_inputs(trace_frame, state.num_players, controllers, is_keyframe=is_keyframe)
        self._append_items(trace_frame, state, is_keyframe=is_keyframe)
        self._frame_count += 1

    def _init_match(self, state: SimFrameState) -> None:
        start = dict(self.match_start or {})
        start.setdefault("traceFrame", 0)
        start.setdefault("simFrameId", int(state.frame_id))
        start.setdefault("randomSeed", int(state.frame_pre_random_seed))
        self.match = {
            "stageId": int(state.stage_id),
            "numPlayers": int(state.num_players),
            "isTeams": bool(state.is_teams),
            "players": [
                {
                    "port": idx + 1,
                    "charId": int(state.char_id[idx]),
                    "teamId": int(state.team_id[idx]),
                }
                for idx in range(state.num_players)
            ],
            "startFrame": 0,
            "start": start,
        }
        self.input_streams = [[] for _ in range(state.num_players)]
        self._prev_inputs = [[] for _ in range(state.num_players)]

    def _append_inputs(
        self,
        trace_frame: int,
        num_players: int,
        controllers: Mapping[int, object],
        *,
        is_keyframe: bool,
    ) -> None:
        for idx in range(num_players):
            row = _input_row(controllers.get(idx + 1))
            previous = self._prev_inputs[idx] or None
            changes = _changed_fields(previous, row)
            if is_keyframe or previous is None:
                self.input_streams[idx].append([OP_KEYFRAME, trace_frame, row])
            elif changes:
                self.input_streams[idx].append([OP_DELTA, trace_frame, changes])
            self._prev_inputs[idx] = row

    def _append_items(self, trace_frame: int, state: SimFrameState, *, is_keyframe: bool) -> None:
        live_slots = set()
        for slot, item in enumerate(state.items):
            if not bool(item["exists"]):
                continue
            live_slots.add(slot)
            row = _item_row(item)
            previous = self._prev_items.get(slot)
            changes = _changed_fields(previous, row)
            if is_keyframe or previous is None:
                self.item_rows.append([OP_KEYFRAME, trace_frame, slot, row])
            elif changes:
                self.item_rows.append([OP_DELTA, trace_frame, slot, changes])
            self._prev_items[slot] = row
        for slot in sorted(set(self._prev_items) - live_slots):
            self.item_rows.append([OP_DELTA, trace_frame, slot, [[0, 0]]])
            del self._prev_items[slot]

    def _append_stage(self, trace_frame: int, state: SimFrameState, *, is_keyframe: bool) -> None:
        row = _stage_row(state)
        changes = _changed_fields(self._prev_stage, row)
        if is_keyframe or self._prev_stage is None:
            self.stage_rows.append([OP_KEYFRAME, trace_frame, row])
        elif changes:
            self.stage_rows.append([OP_DELTA, trace_frame, changes])
        self._prev_stage = row

    def to_payload(self, *, frame_limit: int | None = None) -> dict:
        if self.match is None:
            raise RuntimeError("trace has no frames")
        limit = self._frame_count if frame_limit is None else min(frame_limit, self._frame_count)
        metadata = deepcopy(self.metadata)
        git_info = _git_build_info()
        if git_info is not None:
            provenance = metadata.setdefault("provenance", {})
            if isinstance(provenance, dict):
                provenance.setdefault("git", git_info)
        producer_version = (
            self.producer_version if self.producer_version is not None else _default_producer_version()
        )
        return {
            "format": TRACE_FORMAT,
            "schemaVersion": SCHEMA_VERSION,
            "producer": {
                "name": self.producer_name,
                "version": producer_version,
            },
            "createdAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "match": self.match,
            "inputs": {
                "encoding": SPARSE_DELTA_ENCODING,
                "keyframeInterval": self.keyframe_interval,
                "fields": INPUT_FIELDS,
                "players": [
                    [row for row in stream if int(row[1]) < limit]
                    for stream in self.input_streams
                ],
            },
            "frames": {
                "encoding": SPARSE_DELTA_ENCODING,
                "keyframeInterval": self.keyframe_interval,
                "fields": FRAME_FIELDS,
                "playerFields": PLAYER_FIELDS,
                "rows": [row for row in self.frame_rows if int(row[1]) < limit],
            },
            "stage": {
                "encoding": SPARSE_DELTA_ENCODING,
                "keyframeInterval": self.keyframe_interval,
                "fields": STAGE_FIELDS,
                "rows": [row for row in self.stage_rows if int(row[1]) < limit],
            },
            "items": {
                "encoding": SPARSE_DELTA_ENCODING,
                "keyframeInterval": self.keyframe_interval,
                "fields": ITEM_FIELDS,
                "rows": [row for row in self.item_rows if int(row[1]) < limit],
            },
            "metadata": metadata,
        }

    def write_json(self, path: Path, *, frame_limit: int | None = None) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_payload(frame_limit=frame_limit), separators=(",", ":")) + "\n")
