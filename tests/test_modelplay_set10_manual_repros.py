from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture(name: str) -> dict[str, Any]:
    return json.loads((_root() / "tests" / "fixtures" / "modelplay" / name).read_text())


def _input_bytes() -> np.ndarray:
    binding = importlib.import_module("msl_binding")
    return np.zeros((1, int(binding.sizes()["input"])), dtype=np.uint8)


def _base_seed(fixture: dict[str, Any]) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    players = fixture["players"][:2]
    seed["stage_id"][0] = np.uint32(int(fixture["stage_id"]))
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = [np.uint8(int(p["internal_char_id"])) for p in players]
    seed["team_id"][0, :2] = [np.uint8(int(p["team_id"])) for p in players]
    seed["facing"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["floor_skip_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    seed["floor_skip_segment_valid_u8"][0, :] = np.uint8(0)
    seed["cliff_ledge_floor_segment_id_u16"][0, :] = np.uint16(0xFFFF)
    return seed


def _seed_from_fixture(fixture: dict[str, Any]) -> np.ndarray:
    src = fixture["seed"]
    seed = _base_seed(fixture)
    p = int(fixture["expected"]["player"])
    seed["frame_id"][0] = np.int32(int(src["frame_id"]))
    seed["action_id"][0, p] = np.uint16(int(src["action_id"]))
    seed["animation_index"][0, p] = np.uint32(int(src["submotion_id"]))
    seed["action_frame"][0, p] = np.int16(int(src["action_frame"]))
    seed["anim_frame_f32"][0, p] = np.float32(float(src["anim_frame"]))
    seed["pos_x"][0, p] = np.float32(float(src["pos_x"]))
    seed["pos_y"][0, p] = np.float32(float(src["pos_y"]))
    seed["speed_y_self"][0, p] = np.float32(float(src["speed_y"]))
    seed["speed_air_x_self"][0, p] = np.float32(float(src["speed_air_x"]))
    seed["on_ground"][0, p] = np.uint8(int(src["on_ground"]))
    seed["ground_id"][0, p] = np.uint16(int(src["ground_id"]))
    seed["fall_fast"][0, p] = np.uint8(int(src["fall_fast"]))
    seed["state_flags"][0, p, 1] = np.uint8(int(seed["state_flags"][0, p, 1]) | 0x08)
    seed["floor_sweep_prev_pos_valid_u8"][0, p] = np.uint8(1)
    seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(float(src["floor_sweep_prev_pos_x"]))
    seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(float(src["floor_sweep_prev_pos_y"]))
    return seed


def _step_once(seed: np.ndarray) -> np.void:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        input_t = _input_bytes()
        binding.step_input(handle, input_t, input_t)
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
def test_live_set10_randall_fastfall_lands_instead_of_hovering() -> None:
    # Manual set10 repro: Fox fastfalls onto Randall with neutral stick. The stale source seed still
    # names Yoshi's right platform, while the live floor sweep hits Randall's generated support
    # line. Fall_Coll should publish the ordinary platform landing through
    # ft_800831CC/mpColl_80047E14; the FoD height-transform fastfall suppression does not apply to
    # Randall's static-y stage-object support transform.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
    fixture = _load_fixture("set10_weird_randall_landing_frame_2887_seed.json")
    assert fixture["source_trace"] == "live_capture/set10/weird_randall_landing.json"

    out = _step_once(_seed_from_fixture(fixture))
    p = int(fixture["expected"]["player"])

    assert int(out["action_id"][p]) == int(fixture["expected"]["landing_action_id"])
    assert int(out["animation_index"][p]) == int(fixture["expected"]["landing_submotion_id"])
    assert int(out["action_frame"][p]) == 0
    assert int(out["on_ground"][p]) == 1
    # Randall remains source-owned by the generated MSLSTG01 support line internally, but
    # write_compare serializes Slippi's public raw Yoshi support line.
    assert int(fixture["expected"]["randall_ground_id"]) == 1000
    assert int(out["ground_id"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(fixture["expected"]["randall_y"]))
