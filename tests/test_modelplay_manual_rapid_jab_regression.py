from __future__ import annotations

import copy
import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

RAPID_ENTRY_FIXTURE = "tests/fixtures/modelplay/manual_fox_no_rapid_jabs_prefix_0_205.json"
RAPID_HIT_FIXTURE = (
    "tests/fixtures/modelplay/manual_fox_rapid_jab_hit_cancel_prefix_0_430.json"
)
VANILLA_SMOKE_FIXTURE = "tests/fixtures/modelplay/vanilla_rapid_jab_smoke_windows.json"

ACT_WAIT = 14
ACT_ATTACK_11 = 44
ACT_ATTACK_12 = 45
ACT_ATTACK_100_START = 47
ACT_ATTACK_100_LOOP = 48
ACT_ATTACK_100_END = 49


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture(path: str = RAPID_ENTRY_FIXTURE) -> dict[str, Any]:
    fixture = json.loads((_root() / path).read_text(encoding="utf-8"))
    assert fixture["source_trace"] == "live_capture/fox_no_rapid_jabs.json"
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {
            "start_frame": 125,
            "end_frame": 160,
            "note": (
                "Fox Attack11 -> Attack12 rapid A presses should stay ordinary jab until enough "
                "press/release edges accumulate."
            ),
        },
        {
            "start_frame": 175,
            "end_frame": 205,
            "note": "A later repeat of the same rapid-jab input should enter Attack100Start/Loop from neutral.",
        },
    ]
    return fixture


def _load_hit_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / RAPID_HIT_FIXTURE).read_text(encoding="utf-8"))
    assert (
        fixture["source_trace"]
        == "live_capture/fox_rapid_jab_doesnt_hit_and_easily_cancelled.json"
    )
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {
            "start_frame": 270,
            "end_frame": 306,
            "note": "Fox jabs Falco twice and enters Attack100Start from repeated A press/release edges.",
        },
        {
            "start_frame": 313,
            "end_frame": 409,
            "note": "Attack100Loop hitboxes should repeatedly hit Falco while the loop is sustained.",
        },
        {
            "start_frame": 409,
            "end_frame": 417,
            "note": "After the loop-stop checkpoint, Attack100End remains locked until its own animation completes.",
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


def _load_vanilla_smoke_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / VANILLA_SMOKE_FIXTURE).read_text(encoding="utf-8"))
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert [scenario["name"] for scenario in fixture["scenarios"]] == [
        "short_rapid_jab",
        "long_rapid_jab",
        "short_run_to_falco_rapid_jab",
        "walk068_to_falco_rapid_hit",
    ]
    return fixture


def _input_bytes_from_player_values(
    values_by_player: dict[int, list[float | int]], input_stride: int
) -> np.ndarray:
    arr = np.zeros((1,), dtype=INPUT_DTYPE)
    for player, values in values_by_player.items():
        buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
        arr["p"][0, int(player)]["buttons"] = np.uint16(int(buttons))
        arr["p"][0, int(player)]["main_x"] = _stick_i8(float(main_x))
        arr["p"][0, int(player)]["main_y"] = _stick_i8(float(main_y))
        arr["p"][0, int(player)]["c_x"] = _stick_i8(float(c_x))
        arr["p"][0, int(player)]["c_y"] = _stick_i8(float(c_y))
        arr["p"][0, int(player)]["l"] = np.uint8(
            np.clip(np.rint(float(l_trigger) * 255.0), 0, 255)
        )
        arr["p"][0, int(player)]["r"] = np.uint8(
            np.clip(np.rint(float(r_trigger) * 255.0), 0, 255)
        )
    return arr.view(np.uint8).reshape((1, input_stride))


def _scenario_inputs_by_frame(scenario: dict[str, Any]) -> dict[int, dict[int, list[float | int]]]:
    out: dict[int, dict[int, list[float | int]]] = {}
    for entry in scenario["input_ranges"]:
        player = int(entry["player"])
        values = entry["values"]
        for frame in range(int(entry["start"]), int(entry["end"]) + 1):
            out.setdefault(frame, {})[player] = values
    return out


def _replay_vanilla_scenario(
    fixture: dict[str, Any], scenario: dict[str, Any]
) -> dict[tuple[int, int], dict[str, float | int]]:
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
    history: dict[tuple[int, int], dict[str, float | int]] = {}
    inputs_by_frame = _scenario_inputs_by_frame(scenario)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame_i in range(0, int(scenario["end_frame"]) + 1):
            if frame_i > 0:
                input_t = _input_bytes_from_player_values(
                    inputs_by_frame.get(frame_i, {}), input_stride
                )
                binding.step_input(handle, prev_input, input_t)
                prev_input = input_t.copy()

            binding.write_compare(handle, out_compare)
            row = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            timebase = binding.debug_timebase(handle, 0)
            for player in range(2):
                history[(frame_i, player)] = {
                    "action_id": int(row["action_id"][player]),
                    "action_frame": float(timebase[player, 3]),
                    "pos_x": float(row["pos_x"][player]),
                    "pos_y": float(row["pos_y"][player]),
                    "facing": 1.0 if int(row["facing"][player]) else -1.0,
                    "percent": float(row["percent"][player]),
                    "shield_hp": float(row["shield_hp"][player]),
                    "hitlag": float(row["hitlag"][player]),
                    "on_ground": int(row["on_ground"][player]),
                    "stocks": int(row["stocks"][player]),
                }
    finally:
        binding.destroy(handle)

    return history


def _assert_vanilla_row_matches(
    actual: dict[str, float | int], expected: dict[str, Any], *, scenario_name: str
) -> None:
    frame = int(expected["frame"])
    player = int(expected["player"])
    for field in ("action_id", "on_ground", "stocks"):
        assert int(actual[field]) == int(expected[field]), (
            scenario_name,
            frame,
            player,
            field,
            actual[field],
            expected[field],
        )
    for field in (
        "action_frame",
        "pos_x",
        "pos_y",
        "facing",
        "percent",
        "shield_hp",
        "hitlag",
    ):
        tol = 1e-3 if field == "action_frame" else 1e-4
        assert float(actual[field]) == pytest.approx(float(expected[field]), abs=tol), (
            scenario_name,
            frame,
            player,
            field,
            actual[field],
            expected[field],
        )


@pytest.mark.integration
def test_manual_fox_rapid_jab_reaches_attack100_from_repeated_a_presses() -> None:
    # Source owner: Attack11/12/13 IASA calls ftCo_Attack_800D6A50 before normal jab-chain checks.
    # The helper increments fp+0x1A54 on A press/release edges and enters Attack100Start when the
    # Attack12 script has set x2218_b2 and co_attrs.rapid_jab_window is reached.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{
    #   ftCo_Attack11_IASA,ftCo_Attack12_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
    history = _replay_fixture(_load_fixture(), end_frame=240)

    fox = 0
    assert int(history[125]["action_id"][fox]) == ACT_ATTACK_11
    assert int(history[141]["action_id"][fox]) == ACT_ATTACK_12
    assert all(
        int(history[frame]["action_id"][fox]) != ACT_ATTACK_100_START for frame in range(141, 161)
    )

    assert int(history[165]["action_id"][fox]) == ACT_ATTACK_11
    assert int(history[175]["action_id"][fox]) == ACT_ATTACK_12
    assert int(history[184]["action_id"][fox]) == ACT_ATTACK_100_START
    assert int(history[190]["action_id"][fox]) == ACT_ATTACK_100_LOOP
    assert all(int(history[frame]["action_id"][fox]) == ACT_ATTACK_100_LOOP for frame in range(190, 230))
    assert int(history[230]["action_id"][fox]) == ACT_ATTACK_100_END
    assert int(history[239]["action_id"][fox]) == ACT_WAIT


@pytest.mark.integration
def test_single_jab_press_does_not_enter_attack100_without_rapid_gate() -> None:
    fixture = _load_fixture()
    fixture["input_ranges"] = [fixture["input_ranges"][0]]
    history = _replay_fixture(fixture, end_frame=170)

    fox = 0
    rapid_actions = {
        ACT_ATTACK_100_START,
        ACT_ATTACK_100_LOOP,
        ACT_ATTACK_100_END,
    }
    assert int(history[125]["action_id"][fox]) == ACT_ATTACK_11
    assert int(history[141]["action_id"][fox]) != ACT_ATTACK_100_START
    assert all(int(history[frame]["action_id"][fox]) not in rapid_actions for frame in range(125, 171))
    assert int(history[167]["action_id"][fox]) == ACT_WAIT


@pytest.mark.integration
def test_rapid_jab_path_uses_character_data_for_falco_too() -> None:
    fixture = copy.deepcopy(_load_fixture())
    fixture["players"][0]["internal_char_id"] = 22
    fixture["players"][1]["internal_char_id"] = 1
    history = _replay_fixture(fixture, end_frame=195)

    falco = 0
    assert int(history[125]["action_id"][falco]) == ACT_ATTACK_11
    assert int(history[141]["action_id"][falco]) == ACT_ATTACK_12
    assert int(history[184]["action_id"][falco]) == ACT_ATTACK_100_START
    assert int(history[190]["action_id"][falco]) == ACT_ATTACK_100_LOOP


@pytest.mark.integration
def test_fox_attack100_loop_hits_and_then_locks_into_attack100end() -> None:
    # This is a compact fixture from manual live trace
    # live_capture/fox_rapid_jab_doesnt_hit_and_easily_cancelled.json. It exists because the
    # first rapid-jab implementation exported Attack100Loop move-script hitboxes but left the
    # derived MSLHITB1 runtime hitbox tables stale, so the loop entered but never hit.
    # The compact approach omits the trace's final hard-right movement prefix: source-correct FD
    # edge collision now sends that prefix offstage before this rapid-jab owner is exercised.
    history = _replay_fixture(_load_hit_fixture(), end_frame=430)

    fox = 0
    falco = 1
    assert int(history[306]["action_id"][fox]) == ACT_ATTACK_100_START
    assert int(history[312]["action_id"][fox]) == ACT_ATTACK_100_LOOP
    assert float(history[312]["percent"][falco]) == pytest.approx(8.0)
    assert float(history[314]["percent"][falco]) > 8.0
    # Attack100Loop restart runs ft_800892A0, giving repeated rapid-jab hits fresh attack
    # instances for stale-queue accounting. Runtime HitCapsule clear/copy ownership for ordinary
    # grounded Attack* entries must not be applied to this loop-script cadence; otherwise the loop
    # admits an extra same-window body hit before Attack100End.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_Attack100Loop_Anim,ftCo_800D6C60}
    # refs/melee/src/melee/ft/ft_0881.c::ft_800892A0
    assert float(history[413]["percent"][falco]) == pytest.approx(16.03999900817871)

    assert int(history[413]["action_id"][fox]) == ACT_ATTACK_100_END
    assert all(
        int(history[frame]["action_id"][fox]) == ACT_ATTACK_100_END for frame in range(413, 422)
    )
    assert int(history[422]["action_id"][fox]) == ACT_WAIT


@pytest.mark.integration
def test_removed_rapid_jab_hard_right_prefix_now_correctly_falls_off_fd_edge() -> None:
    # Packageability guard for the compact rapid-jab fixture split: the omitted hard-right
    # approach prefix is no longer part of the Attack100 hitbox owner because source-correct FD
    # edge collision sends Fox offstage before the rapid-jab window.
    fixture = copy.deepcopy(_load_hit_fixture())
    fixture["input_ranges"].insert(4, [181, 188, [0, 1, 0, 0, 0, 0, 0]])
    history = _replay_fixture(fixture, end_frame=306)

    fox = 0
    assert int(history[235]["action_id"][fox]) != ACT_ATTACK_100_START
    assert float(history[270]["pos_y"][fox]) < -130.0
    assert int(history[306]["action_id"][fox]) != ACT_ATTACK_100_START


@pytest.mark.integration
def test_rapid_jab_manual_sequences_match_vanilla_reference_windows() -> None:
    # Compact reference rows from vanilla/Dolphin smoke runs. The source trace and engine-dump
    # paths in the fixture are metadata only; this test replays the compact input ranges locally
    # and compares against selected vanilla rows around Attack100 entry, long-loop lock-in, run/walk
    # prefixes, repeated BODY hits, combo-push drift, and Attack100End.
    fixture = _load_vanilla_smoke_fixture()
    for scenario in fixture["scenarios"]:
        history = _replay_vanilla_scenario(fixture, scenario)
        scenario_name = str(scenario["name"])
        for expected in scenario["vanilla_rows"]:
            frame = int(expected["frame"])
            player = int(expected["player"])
            _assert_vanilla_row_matches(
                history[(frame, player)], expected, scenario_name=scenario_name
            )
