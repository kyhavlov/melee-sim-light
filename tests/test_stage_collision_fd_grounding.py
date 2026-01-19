from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


def _step_once(seed: np.ndarray) -> np.ndarray:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)

        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        # Avoid leaks in long-running test sessions.
        msl_binding.destroy(handle)
        del handle


def _seed_base(*, stage_id: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    return seed


def test_fd_boundary_x_minus60_picks_mid_segment() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(-60.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 1


def test_fd_boundary_x_60_picks_right_segment() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(60.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 5


def test_vy_positive_never_grounds() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(1.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(1234)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 1234


def test_vy_zero_can_ground_when_on_surface() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1


def test_no_snap_from_far_below_stage() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(-10.0)
    seed["speed_y_self"][0, 0] = np.float32(-0.1)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(2222)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 2222


def test_ground_id_unchanged_when_airborne() -> None:
    seed = _seed_base(stage_id=32)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(5.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(3333)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == 3333


def test_unsupported_stage_is_noop_for_on_ground() -> None:
    seed = _seed_base(stage_id=1)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(4444)

    out = _step_once(seed)
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4444
