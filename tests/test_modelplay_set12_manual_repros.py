from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array


FIXTURE_YOSHI_SLOPED_LEDGE_DASH = Path(
    "tests/fixtures/modelplay/set12_yoshi_sloped_ledge_dash_no_cliffcatch_prefix_0_200.json"
)

ACT_DASH = 20
ACT_TURN = 18
ACT_FALL = 29
ACT_CLIFF_CATCH = 252


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _input_for_frame(ranges: list[list[Any]], frame: int) -> list[float | int]:
    for start, end, values in ranges:
        if int(start) <= frame <= int(end):
            return list(values)
    raise AssertionError(f"missing input range for frame {frame}")


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _run_fixture(fixture_path: Path) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    fixture = json.loads((_root() / fixture_path).read_text(encoding="utf-8"))
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]

    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)

    players = fixture["players"]
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["seed"]),
    )

    history: dict[int, np.void] = {}
    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        if not hasattr(binding, "debug_set_player_root"):
            pytest.skip("msl_binding debug_set_player_root unavailable")
        for player, row in enumerate(players):
            binding.debug_set_player_root(
                handle,
                0,
                player,
                float(row["x"]),
                float(row["y"]),
                int(row["facing"]),
            )

        for frame_i in range(0, int(fixture["end_frame"]) + 1):
            binding.write_compare(handle, out_compare)
            history[frame_i] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if frame_i == int(fixture["end_frame"]):
                break

            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            _write_input_player(input_t, 0, _input_for_frame(fixture["p1_ranges"], frame_i + 1))
            _write_input_player(input_t, 1, _input_for_frame(fixture["p2_ranges"], frame_i + 1))
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_yoshi_sloped_ledge_dash_falls_past_ledge_without_false_cliffcatch() -> None:
    history = _run_fixture(FIXTURE_YOSHI_SLOPED_LEDGE_DASH)

    # Manual set12 repro:
    # manual_repros/set12/weird_grounded_movement.msltrace.json, Fox around frames 167-195.
    #
    # Corrected vanilla probe:
    # reports/triage/weird_grounded_movement_vanilla_probe_fixed/vanilla_engine_dump_clear.bin
    #
    # Vanilla rides Yoshi's right sloped ledge floor during Dash, then a Turn_IASA just-turned
    # branch enters Dash before Dash_Coll loses the floor. The resulting Fall keeps the Dash
    # source-facing lane and never enters CliffCatch behind the outgoing trajectory.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
    assert int(history[182]["action_id"][0]) == ACT_DASH
    assert int(history[182]["on_ground"][0]) == 1
    assert int(history[183]["action_id"][0]) == ACT_TURN

    for frame_i in range(184, 201):
        assert int(history[frame_i]["action_id"][0]) == ACT_FALL
        assert int(history[frame_i]["action_id"][0]) != ACT_CLIFF_CATCH
        assert int(history[frame_i]["on_ground"][0]) == 0
        assert int(history[frame_i]["facing"][0]) == 1

    assert float(history[195]["pos_y"][0]) < -18.0
    assert float(history[200]["pos_y"][0]) < -30.0
