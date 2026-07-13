from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE

ACT_DAMAGE_FLY_TOP = 0x005A
ACT_WAIT = 0x000E
SM_DAMAGE_FLY_TOP = 180
SM_WAIT1_0 = 2


def _step_seed(seed: np.ndarray, steps: int = 1) -> np.void:
    import pytest

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        prev_input = np.zeros((1, input_stride), dtype=np.uint8)
        input_now = np.zeros((1, input_stride), dtype=np.uint8)
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        for _ in range(steps):
            binding.step_input(handle, prev_input, input_now)
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _base_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.array([1, 22], dtype=np.uint8)
    seed["handicap"][0, :2] = np.uint8(9)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.array([1, 0], dtype=np.uint8)
    seed["facing_dir1"][0, :2] = np.array([1.0, -1.0], dtype=np.float32)
    seed["pos_x"][0, :2] = np.array([0.0, 0.0], dtype=np.float32)
    seed["pos_y"][0, :2] = np.array([0.0, -3.0], dtype=np.float32)
    seed["on_ground"][0, :2] = np.array([1, 1], dtype=np.uint8)
    seed["ground_id"][0, :2] = np.uint16(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    return seed


def test_stale_damagefly_ground_id_without_release_colldata_last_pos_does_not_publish() -> None:
    # Public ground_id plus a below-floor root is not release-local source authority. Without the
    # live ftCo_800DDDE4 release callback, stale seed state must not manufacture floor contact.
    seed = _base_seed()
    seed["action_id"][0, :2] = np.array([ACT_WAIT, ACT_DAMAGE_FLY_TOP], dtype=np.uint16)
    seed["animation_index"][0, :2] = np.array([SM_WAIT1_0, SM_DAMAGE_FLY_TOP], dtype=np.uint32)
    seed["anim_frame_f32"][0, :2] = np.float32(1.0)
    seed["action_frame"][0, :2] = np.int16(1)
    seed["on_ground"][0, 1] = np.uint8(0)
    seed["floor_sweep_prev_pos_x_f32"][0, 1] = np.float32(0.0)
    seed["floor_sweep_prev_pos_y_f32"][0, 1] = np.float32(-3.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 1] = np.uint8(1)

    out = _step_seed(seed, steps=1)

    assert int(out["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert int(out["on_ground"][1]) == 0
    assert float(out["pos_y"][1]) < 0.0
