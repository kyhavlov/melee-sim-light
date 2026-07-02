from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FX_SPECIAL_AIR_HI = 0x0164
SM_WAIT = 2
SM_FX_SPECIAL_HI = 309
STAGE_FD = 32
CHAR_FOX = 1


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _call(binding, name: str, *args):
    fn = getattr(binding, name, None)
    if fn is None:
        fn = getattr(binding, f"{name}_v0")
    return fn(*args)


def _destroy(binding, handle) -> None:
    fn = getattr(binding, "destroy", None)
    if fn is not None:
        fn(handle)


def test_specialairhi_reverse_accel_starts_at_x70_threshold() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    fox = json.loads((root / "data/characters/fox.json").read_text())

    reverse_start = int(fox["firefox_launch_reverse_accel_start_frames"])
    duration = int(fox["firefox_launch_duration_frames"])
    reverse_accel = float(fox["firefox_launch_reverse_accel"])
    launch_speed = float(fox["firefox_launch_speed"])
    assert reverse_start > 0
    assert duration > reverse_start

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_AIR_HI)
    seed["animation_index"][0, 0] = np.uint32(SM_FX_SPECIAL_HI)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_frame"][0, 0] = np.int16(min(duration - 2, reverse_start - 1))
    seed["anim_frame_f32"][0, 0] = np.float32(float(seed["action_frame"][0, 0]))
    seed["facing"][0, 0] = np.uint8(1)
    seed["speed_air_x_self"][0, 0] = np.float32(launch_speed)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 1] = np.uint16(0)
    seed["pos_y"][0, 1] = np.float32(0.0001)

    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
    input_t = np.zeros((1,), dtype=INPUT_DTYPE)
    prev_input_bytes = prev_input.view(np.uint8).reshape((1, input_stride))
    input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        _call(binding, "reseed_seed", handle, seed_bytes)
        _call(binding, "step_input", handle, prev_input_bytes, input_bytes)
        _call(binding, "write_compare", handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        _destroy(binding, handle)

    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_HI
    assert abs(float(out["speed_air_x_self"][0]) - (launch_speed - reverse_accel)) <= 1e-4
    assert abs(float(out["speed_y_self"][0])) <= 1e-4
