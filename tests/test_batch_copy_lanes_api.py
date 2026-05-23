from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
SM_WAIT1_0 = 2
CHAR_FOX = 1
STAGE_FD = 32


def _seed_rows(batch_size: int) -> np.ndarray:
    seed = np.zeros((batch_size,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(STAGE_FD)
    seed["num_players"] = np.uint8(2)
    seed["frame_id"] = np.arange(100, 100 + batch_size, dtype=np.int32)
    seed["frame_pre_random_seed"] = np.arange(1000, 1000 + batch_size, dtype=np.uint32)
    seed["stocks"][:, :2] = np.uint8(4)
    seed["char_id"][:, :2] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][:, :2] = np.float32(1.0)
    seed["defense_ratio"][:, :2] = np.float32(1.0)
    seed["fighter_scale_y"][:, :2] = np.float32(1.0)
    seed["frame_speed_mul_f32"][:, :2] = np.float32(1.0)
    seed["facing"][:, :2] = np.uint8(1)
    seed["on_ground"][:, :2] = np.uint8(1)
    seed["ground_id"][:, :2] = np.uint16(0)
    seed["action_id"][:, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][:, :2] = np.uint32(SM_WAIT1_0)
    for lane in range(batch_size):
        seed["pos_x"][lane, 0] = np.float32(-70.0 + 10.0 * lane)
        seed["pos_x"][lane, 1] = np.float32(70.0 - 10.0 * lane)
        seed["percent"][lane, 0] = np.float32(5.0 * lane)
        seed["percent"][lane, 1] = np.float32(20.0 + 7.0 * lane)
        seed["instance_id"][lane, 0] = np.uint16(100 + lane)
        seed["instance_id"][lane, 1] = np.uint16(200 + lane)
    return seed


def _seed_bytes(binding, seed: np.ndarray) -> np.ndarray:
    seed_stride = int(binding.sizes()["seed"])
    assert seed_stride == SEED_DTYPE.itemsize
    return seed.view(np.uint8).reshape((seed.shape[0], seed_stride))


def _write_compare(binding, handle, batch_size: int) -> np.ndarray:
    compare_stride = int(binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((batch_size, compare_stride), dtype=np.uint8)
    binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((batch_size,)).copy()


def _init_seeded(binding, seed: np.ndarray):
    handle = binding.init(batch_size=seed.shape[0], num_players=2)
    binding.reseed_seed(handle, _seed_bytes(binding, seed))
    return handle


def _assert_public_lane_equal(actual: np.void, expected: np.void) -> None:
    for field in (
        "frame_id",
        "frame_pre_random_seed",
        "stage_id",
        "num_players",
        "char_id",
        "pos_x",
        "pos_y",
        "percent",
        "stocks",
        "action_id",
        "animation_index",
        "instance_id",
    ):
        np.testing.assert_array_equal(actual[field], expected[field])


def test_copy_lanes_between_different_batches() -> None:
    binding = pytest.importorskip("msl_binding")
    src_seed = _seed_rows(3)
    dst_seed = _seed_rows(3)
    dst_seed["frame_id"] += np.int32(10000)
    dst_seed["percent"] += np.float32(100.0)

    src = _init_seeded(binding, src_seed)
    dst = _init_seeded(binding, dst_seed)
    try:
        src_before = _write_compare(binding, src, 3)
        binding.debug_copy_lanes(
            dst,
            src,
            np.array([0, 1, 2], dtype=np.int32),
            np.array([2, 0, 1], dtype=np.int32),
        )
        dst_after = _write_compare(binding, dst, 3)
    finally:
        binding.destroy(src)
        binding.destroy(dst)

    for dst_lane, src_lane in enumerate([2, 0, 1]):
        _assert_public_lane_equal(dst_after[dst_lane], src_before[src_lane])


def test_copy_lanes_same_batch_exact_self_copy_is_allowed() -> None:
    binding = pytest.importorskip("msl_binding")
    handle = _init_seeded(binding, _seed_rows(3))
    try:
        before = _write_compare(binding, handle, 3)
        binding.debug_copy_lanes(
            handle,
            handle,
            np.array([0, 1, 2], dtype=np.int32),
            np.array([0, 1, 2], dtype=np.int32),
        )
        after = _write_compare(binding, handle, 3)
    finally:
        binding.destroy(handle)

    for lane in range(3):
        _assert_public_lane_equal(after[lane], before[lane])


def test_copy_lanes_same_batch_duplicate_source_broadcast_is_allowed() -> None:
    binding = pytest.importorskip("msl_binding")
    handle = _init_seeded(binding, _seed_rows(3))
    try:
        before = _write_compare(binding, handle, 3)
        binding.debug_copy_lanes(
            handle,
            handle,
            np.array([1, 2], dtype=np.int32),
            np.array([0, 0], dtype=np.int32),
        )
        after = _write_compare(binding, handle, 3)
    finally:
        binding.destroy(handle)

    _assert_public_lane_equal(after[0], before[0])
    _assert_public_lane_equal(after[1], before[0])
    _assert_public_lane_equal(after[2], before[0])


@pytest.mark.parametrize(
    ("dst_lanes", "src_lanes"),
    [
        ([1, 2], [0, 1]),
        ([0, 1], [1, 0]),
    ],
)
def test_copy_lanes_same_batch_hazardous_overlap_is_rejected(
    dst_lanes: list[int], src_lanes: list[int]
) -> None:
    binding = pytest.importorskip("msl_binding")
    handle = _init_seeded(binding, _seed_rows(3))
    try:
        before = _write_compare(binding, handle, 3)
        with pytest.raises(ValueError, match="msl_batch_copy_lanes failed"):
            binding.debug_copy_lanes(
                handle,
                handle,
                np.array(dst_lanes, dtype=np.int32),
                np.array(src_lanes, dtype=np.int32),
            )
        after = _write_compare(binding, handle, 3)
    finally:
        binding.destroy(handle)

    for lane in range(3):
        _assert_public_lane_equal(after[lane], before[lane])
