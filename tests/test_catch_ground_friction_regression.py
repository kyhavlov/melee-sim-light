from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_CATCH = 0x00D4
SM_WAIT = 2
SM_CATCH = 242
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


def test_catch_grounded_phys_uses_ftcommon_x64_friction() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    fox = json.loads((root / "data/characters/fox.json").read_text())
    common = json.loads((root / "data/common/ft_common_data.json").read_text())

    gr_friction = float(fox["gr_friction"])
    catch_friction_mul = float(common["catch_friction_mul"])
    seed_speed = 1.6925
    expected_speed = seed_speed - min(seed_speed, catch_friction_mul * gr_friction)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_CATCH)
    seed["animation_index"][0, 0] = np.uint32(SM_CATCH)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["speed_ground_x_self"][0, 0] = np.float32(seed_speed)
    seed["speed_air_x_self"][0, 0] = np.float32(seed_speed)
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

    assert int(out["action_id"][0]) == ACT_CATCH
    assert abs(float(out["speed_ground_x_self"][0]) - expected_speed) <= 1e-5
    assert abs(float(out["pos_x"][0]) - expected_speed) <= 1e-5
