from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

FIXTURE = "tests/fixtures/modelplay/manual_respawn_odd_location_after_up_b_below_wall_prefix_0_448.json"


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    assert fixture["source_trace"] == "manual_repros/respawn_odd_location_after_up_b_below_wall.json"
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert fixture["windows"] == [
        {
            "start_frame": 202,
            "end_frame": 313,
            "note": "Fox Up-Bs below the left FD underside, then dies from the bottom blastzone.",
        },
        {
            "start_frame": 373,
            "end_frame": 432,
            "note": "Rebirth descends from camera top to the respawn platform.",
        },
        {
            "start_frame": 433,
            "end_frame": 448,
            "note": "Down-held RebirthWait exit enters Fall without stale underside ceiling projection.",
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
            # Webplay input traces store the controller sample with the output frame number.
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
def test_rebirthwait_drop_does_not_reuse_pre_death_ceiling_contact() -> None:
    # Manual webplay repro: Fox Up-B hits the FD underside before dying. The old runtime left
    # CollData's ceiling id/env flags live through Dead*/Rebirth, so the first Fall frame after
    # a down-held RebirthWait exit projected the respawn position back down to the underside.
    #
    # Source owner:
    # - Dead* -> Rebirth runs Fighter_UnkProcessDeath_80068354 / Fighter_UnkInitReset_80067C98.
    # - Rebirth/RebirthWait use dedicated collision callbacks instead of common Fall CollData
    #   ceiling persistence.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_UnkProcessDeath_80068354,Fighter_UnkInitReset_80067C98}
    # refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_Rebirth_Coll,ftCo_RebirthWait_Coll}
    fixture = _load_fixture()
    history = _replay_fixture(fixture, end_frame=448)

    fox = 0
    assert int(history[313]["action_id"][fox]) == 0  # DeadDown from bottom blastzone.
    assert float(history[313]["pos_y"][fox]) < -140.0

    assert int(history[432]["action_id"][fox]) == 12  # Rebirth reaches platform height.
    assert float(history[432]["pos_y"][fox]) == pytest.approx(45.0, abs=1e-3)

    assert int(history[433]["action_id"][fox]) == 29  # RebirthWait IASA exits to Fall.
    assert float(history[433]["pos_y"][fox]) == pytest.approx(44.77, abs=1e-3)
    assert float(history[433]["pos_y"][fox]) > 0.0

    for frame in range(433, 448):
        assert int(history[frame]["action_id"][fox]) == 29
        assert float(history[frame]["pos_y"][fox]) > -5.0
        assert int(history[frame]["on_ground"][fox]) == 0

    # The input is held down long before respawn, so the source x671 tilt timer is stale and Fall
    # must not latch fastfall immediately after RebirthWait. The important regression lock is that
    # the first Fall frames remain above stage rather than reusing stale underside ceiling contact.
    assert int(history[448]["action_id"][fox]) == 29
    assert float(history[448]["pos_y"][fox]) > 0.0
    assert int(history[448]["state_flags"][fox][1]) & 0x08 == 0
    assert int(history[448]["on_ground"][fox]) == 0
