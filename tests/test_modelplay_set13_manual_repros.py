from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

ACT_ESCAPE_AIR = 236
ACT_PASSIVE_WALL_JUMP = 203

SET13_DIR = Path("manual_repros/set13")


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_trace(name: str) -> dict[str, Any]:
    trace = json.loads((_root() / SET13_DIR / name).read_text(encoding="utf-8"))
    assert trace["format"] == "MSLTRACE1"
    assert trace["inputs"]["fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    return trace


def _decode_input_streams(trace: dict[str, Any], frame_count: int) -> list[list[list[float | int]]]:
    streams: list[list[list[float | int]]] = []
    for rows in trace["inputs"]["players"]:
        current: list[float | int] = [0, 0, 0, 0, 0, 0, 0]
        row_i = 0
        decoded: list[list[float | int]] = []
        for frame in range(frame_count):
            while row_i < len(rows) and int(rows[row_i][1]) <= frame:
                if int(rows[row_i][1]) != frame:
                    break
                if int(rows[row_i][0]) == 0:
                    current = list(rows[row_i][2])
                else:
                    current = list(current)
                    for field_i, value in rows[row_i][2]:
                        current[int(field_i)] = value
                row_i += 1
            decoded.append(list(current))
        streams.append(decoded)
    return streams


def _stick_i8(value: float | int) -> np.int8:
    return np.int8(np.clip(np.rint(float(value) * 80.0), -80, 80))


def _trigger_u8(value: float | int) -> np.uint8:
    return np.uint8(np.clip(np.rint(float(value) * 255.0), 0, 255))


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(main_x)
    arr["p"][0, player]["main_y"] = _stick_i8(main_y)
    arr["p"][0, player]["c_x"] = _stick_i8(c_x)
    arr["p"][0, player]["c_y"] = _stick_i8(c_y)
    arr["p"][0, player]["l"] = _trigger_u8(l_trigger)
    arr["p"][0, player]["r"] = _trigger_u8(r_trigger)


def _replay_trace(trace: dict[str, Any]) -> list[np.void]:
    binding = pytest.importorskip("msl_binding")
    frame_count = max(int(row[1]) for row in trace["frames"]["rows"]) + 1
    inputs = _decode_input_streams(trace, frame_count + 1)
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    players = trace["match"]["players"]
    config = build_match_config_array(
        num_players=int(trace["match"]["numPlayers"]),
        char_ids=tuple(int(p["charId"]) for p in players),
        team_ids=tuple(int(p["teamId"]) for p in players),
        facing=(1, 0),
        stage_id=int(trace["match"]["stageId"]),
        frame_id=0,
        random_seed=int(trace["match"]["start"]["randomSeed"]),
    )

    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: list[np.void] = []

    handle = binding.init(batch_size=1, num_players=int(trace["match"]["numPlayers"]))
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame_i in range(frame_count):
            binding.write_compare(handle, out_compare)
            history.append(out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy())
            if frame_i == frame_count - 1:
                break
            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            for player_i in range(int(trace["match"]["numPlayers"])):
                _write_input_player(input_t, player_i, inputs[player_i][frame_i + 1])
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_marth_airdodge_trace_does_not_snap_to_far_ledge_wall() -> None:
    history = _replay_trace(_load_trace("marth airdodge teleport.json"))

    # Webplay set13 incident: old runtime entered EscapeAir at frame 1729 and projected Marth from
    # x=47.945679 to FD's left wall/ledge x=-85.565689 through stale carried-wall provenance.
    #
    # EscapeAir_Coll consumes the current mpColl wall/ledge callback packet; the adjacent-wall
    # handoff is bounded by current ECB side penetration rather than by the carried floor endpoint.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80046904}
    before = history[1728]
    after = history[1729]
    assert int(after["action_id"][0]) == ACT_ESCAPE_AIR
    assert float(before["pos_x"][0]) > 40.0
    assert float(after["pos_x"][0]) > 40.0
    assert math.hypot(
        float(after["pos_x"][0]) - float(before["pos_x"][0]),
        float(after["pos_y"][0]) - float(before["pos_y"][0]),
    ) < 4.0


@pytest.mark.integration
def test_marth_walljump_trace_never_enters_passive_wall_jump() -> None:
    history = _replay_trace(_load_trace("marth_bair_through_ledge_wall_jump.msltrace.json"))

    # Marth's extracted character params have can_walljump=false; runtime walljump admission must
    # honor that same data instead of using nonzero walljump numeric thresholds as a proxy.
    # data/characters/marth.json::can_walljump
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    assert all(int(row["action_id"][0]) != ACT_PASSIVE_WALL_JUMP for row in history)
