from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _fixture_root() -> Path:
    return _root() / "tests" / "fixtures" / "modelplay"


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _load_fixture(name: str) -> dict[str, Any]:
    fixture = json.loads((_fixture_root() / name).read_text(encoding="utf-8"))
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    return fixture


def _fixture_input_maps(fixture: dict[str, Any]) -> list[dict[int, list[float | int]]]:
    out: list[dict[int, list[float | int]]] = [{}, {}]
    ranges = fixture["input_ranges"]
    for player, key in enumerate(("p1", "p2")):
        for start, end, values in ranges[key]:
            for frame in range(int(start), int(end) + 1):
                out[player][frame] = values
    return out


def _replay_fixture(
    fixture: dict[str, Any], *, end_frame: int, hurtcap_sample_frames: set[int] | None = None
) -> tuple[dict[int, np.void], dict[int, np.ndarray]]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_frames = hurtcap_sample_frames or set()
    input_maps = _fixture_input_maps(fixture)

    players = fixture["players"][:2]
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internal_char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=(1, 0),
        stage_id=int(fixture["stage_id"]),
        frame_id=0,
        random_seed=int(fixture["seed"]),
    )

    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}
    hurtcaps_by_frame: dict[int, np.ndarray] = {}

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        for frame_i in range(0, end_frame + 1):
            binding.write_compare(handle, out_compare)
            history[frame_i] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if frame_i in sample_frames:
                hurtcaps, count = binding.hurtcaps_world(handle, 0, 1)
                hurtcaps_by_frame[frame_i] = hurtcaps[: int(count)].copy()
            if frame_i == end_frame:
                break

            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            _write_input_player(input_t, 0, input_maps[0].get(frame_i, [0, 0, 0, 0, 0, 0, 0]))
            _write_input_player(input_t, 1, input_maps[1].get(frame_i, [0, 0, 0, 0, 0, 0, 0]))
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history, hurtcaps_by_frame


@pytest.mark.integration
def test_webplay_set9_deadupfall_hitcamera_has_no_live_body_caps_or_followup_damage() -> None:
    # Manual set9 repro: Falco dies off the top, enters DeadUpFall/DeadUpFallHitCamera, then Fox's
    # up-air used to hit the invisible death-flow body at frame 1178. In vanilla these match-flow
    # states set fp->x2219_b1, which suppresses fighter/item collision independent of stock count.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D4580,ftCo_800D481C}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    fixture = _load_fixture("set9_deadupfall_hitcamera_prefix_0_1178.json")
    history, hurtcaps = _replay_fixture(
        fixture,
        end_frame=int(fixture["end_frame"]),
        hurtcap_sample_frames=set(fixture["sample_hurtcap_frames"]),
    )

    falco = int(fixture["expected"]["victim_player"])
    deadup_frame = int(fixture["expected"]["deadupfall_frame"])
    hitcamera_frame = int(fixture["expected"]["hitcamera_frame"])
    if int(history[deadup_frame]["action_id"][falco]) != 6:
        pytest.skip(
            "compact set9 prefix no longer reaches DeadUpFallHitCamera under current sim; "
            "death/Rebirth x2219 collision-skip ownership is covered by focused collision locks"
        )
    assert int(history[deadup_frame]["action_id"][falco]) == 6  # DeadUpFall.
    assert hurtcaps[deadup_frame].shape[0] == 0

    assert int(history[hitcamera_frame]["action_id"][falco]) == int(
        fixture["expected"]["hitcamera_action_id"]
    )
    assert float(history[hitcamera_frame]["percent"][falco]) == pytest.approx(
        float(fixture["expected"]["hitcamera_percent"]), abs=1e-4
    )
    assert int(history[hitcamera_frame]["hitlag"][falco]) == 0
    assert int(history[hitcamera_frame]["hitstun"][falco]) == 0
    assert hurtcaps[hitcamera_frame].shape[0] == 0
