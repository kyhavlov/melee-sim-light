from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_CLIFF_ESCAPE_QUICK = 0x0103
SM_WAIT = 2
SM_CLIFF_ESCAPE_QUICK = 224
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


def test_grounded_cliff_escape_quick_no_longer_pins_against_ledge_wall() -> None:
    binding = pytest.importorskip("msl_binding")

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    # Direct repro from the grounded ledge-roll viewer window:
    # reports/modelplay/20260409_rl_doubles_v27_7000_rerun2/trace.json frame 2365 p1
    seed["action_id"][0, 0] = np.uint16(ACT_CLIFF_ESCAPE_QUICK)
    seed["animation_index"][0, 0] = np.uint32(SM_CLIFF_ESCAPE_QUICK)
    seed["action_frame"][0, 0] = np.int16(21)
    seed["anim_frame_f32"][0, 0] = np.float32(21.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["facing"][0, 0] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(-85.5656967163086)
    seed["pos_y"][0, 0] = np.float32(0.0001)

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

    assert int(out["action_id"][0]) == ACT_CLIFF_ESCAPE_QUICK
    assert int(out["on_ground"][0]) == 1
    assert float(out["pos_x"][0]) > float(seed["pos_x"][0, 0]) + 0.25
