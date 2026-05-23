from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FALL = 0x001D
SM_WAIT1_0 = 2
SM_FALL = 20
CHAR_FOX = 1
STAGE_FD = 32


def test_airborne_grounded_locomotion_restored_state_enters_fall() -> None:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["seed_prev_action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["seed_prev_action_frame"][0, :2] = np.int16(1)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["on_ground"][0, :2] = np.uint8(0)
    seed["ground_id"][0, :2] = np.uint16(0xFFFF)
    seed["pos_y"][0, :2] = np.float32(25.0)

    handle = binding.init(batch_size=1, num_players=2)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.debug_run_locomotion_post_collision(handle)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)

    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["animation_index"][0]) == SM_FALL
    assert int(out["on_ground"][0]) == 0


def test_airborne_grounded_locomotion_first_public_seed_without_history_is_not_rewritten() -> None:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["on_ground"][0, :2] = np.uint8(0)
    seed["ground_id"][0, :2] = np.uint16(0xFFFF)
    seed["pos_y"][0, :2] = np.float32(25.0)

    handle = binding.init(batch_size=1, num_players=2)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.debug_run_locomotion_post_collision(handle)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)

    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["animation_index"][0]) == SM_WAIT1_0
