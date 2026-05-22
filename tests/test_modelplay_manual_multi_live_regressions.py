from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

FIXTURE = "tests/fixtures/modelplay/manual_multi_live_locomotion_specialhi_prefix_0_2460.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    assert fixture["source_trace"] == "live_capture/multi_live.json"
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {
            "start_frame": 316,
            "end_frame": 340,
            "note": (
                "Fox Run -> TurnRun should finish into Run with post-flip facing when stick stays "
                "opposite old facing."
            ),
        },
        {
            "start_frame": 2159,
            "end_frame": 2258,
            "note": (
                "Grounded horizontal Firefox leaves SpecialHi into FallSpecial with all jumps "
                "consumed before recovery fall."
            ),
        },
    ]
    return fixture


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _input_bytes_from_compact(values: list[float | int], input_stride: int) -> np.ndarray:
    arr = np.zeros((1,), dtype=INPUT_DTYPE)
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, 0]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, 0]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, 0]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, 0]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, 0]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, 0]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, 0]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))
    return arr.view(np.uint8).reshape((1, input_stride))


def _inputs_by_frame(fixture: dict[str, Any]) -> dict[int, list[float | int]]:
    out: dict[int, list[float | int]] = {}
    for start, end, values in fixture["input_ranges"]:
        for frame in range(int(start), int(end) + 1):
            out[frame] = values
    return out


def _replay_fixture(fixture: dict[str, Any], *, end_frame: int) -> dict[int, np.void]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    players = sorted(fixture["players"], key=lambda p: int(p["slot"]))
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internal_char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["seed"]),
    )
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}
    inputs_by_frame = _inputs_by_frame(fixture)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        binding.write_compare(handle, out_compare)
        history[0] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        for frame_i in range(0, end_frame):
            # Live viewer input traces store the controller sample with the output frame number.
            input_t = _input_bytes_from_compact(
                inputs_by_frame.get(frame_i + 1, [0, 0, 0, 0, 0, 0, 0]), input_stride
            )
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare)
            history[frame_i + 1] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            prev_input = input_t.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_run_turnaround_finishes_into_run_with_post_flip_facing() -> None:
    # Source owner: ftCo_TurnRun_Anim flips facing after the script-owned pivot gate, then the
    # animation-end path calls fn_800CA644 against that post-flip facing before falling back to Wait.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644
    history = _replay_fixture(_load_fixture(), end_frame=350)

    fox = 0
    assert int(history[316]["action_id"][fox]) == 19  # TurnRun entry from Run.
    assert int(history[316]["facing"][fox]) == 0
    assert int(history[335]["action_id"][fox]) == 19
    assert int(history[335]["facing"][fox]) == 1

    # The script-owned pivot freeze delays the animation-end handoff, but the destination remains
    # post-flip Run rather than Wait -> Turn.
    assert int(history[345]["action_id"][fox]) == 21
    assert int(history[345]["facing"][fox]) == 1
    assert int(history[346]["action_id"][fox]) == 21


@pytest.mark.integration
def test_specialhi_fallspecial_consumes_jumps_before_recovery_fall() -> None:
    # Source owner: ftCo_80096900(..., unk=true) consumes all jumps when SpecialHi enters
    # FallSpecial, via ftCommon_8007D60C on grounded source states or ftCommon_UseAllJumps in air.
    # The manual trace presses X during the recovery fall; this must not become JumpAerial.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D60C,ftCommon_UseAllJumps}
    history = _replay_fixture(_load_fixture(), end_frame=2455)

    fox = 0
    assert int(history[2159]["action_id"][fox]) == 353  # Grounded SpecialHi.
    assert int(history[2251]["action_id"][fox]) == 35  # FallSpecial.
    assert int(history[2251]["jumps_left"][fox]) == 0

    for frame in range(2251, 2259):
        assert int(history[frame]["action_id"][fox]) != 28  # No JumpAerialB during recovery.
        assert int(history[frame]["jumps_left"][fox]) == 0

    assert int(history[2450]["action_id"][fox]) == 0  # Continues falling to blastzone death.
