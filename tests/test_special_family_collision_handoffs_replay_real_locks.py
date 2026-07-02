from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


def test_specialhilanding_phys_large_ground_velocity_applies_friction_without_clearing() -> None:
    # Synthetic control for SpecialHiLanding_Phys:
    # - ftFx_SpecialHiLanding_Phys applies character x7C ground momentum friction through
    #   ftCommon_ApplyFrictionGround, then common ground movement.
    # - Velocities larger than x7C must remain nonzero after one friction step.
    # - Fox and Falco share the extracted `firefox_ground_momentum_end` char-param path.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_ApplyFrictionGround,ftCommon_ApplyGroundMovement}
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    for char_id in (1, 22):
        seed = np.zeros((1,), dtype=SEED_DTYPE)
        seed["stage_id"][0] = np.uint32(32)
        seed["num_players"][0] = np.uint8(2)
        seed["char_id"][0, :2] = np.uint8(char_id)
        seed["stocks"][0, :2] = np.uint8(4)
        seed["action_id"][0, 0] = np.uint16(357)  # SpecialHiLanding.
        seed["animation_index"][0, 0] = np.uint32(310)
        seed["action_frame"][0, 0] = np.int16(1)
        seed["anim_frame_f32"][0, 0] = np.float32(1.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["ground_id"][0, 0] = np.uint16(0)
        seed["speed_ground_x_self"][0, 0] = np.float32(2.0)
        seed["speed_air_x_self"][0, 0] = np.float32(2.0)
        seed["action_id"][0, 1] = np.uint16(14)  # Wait.
        seed["animation_index"][0, 1] = np.uint32(2)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["ground_id"][0, 1] = np.uint16(0)

        prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
        cur_input = np.zeros((1,), dtype=INPUT_DTYPE)
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
        try:
            binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, seed_stride))
            binding.step_input(
                handle,
                prev_input.view(np.uint8).reshape(1, input_stride),
                cur_input.view(np.uint8).reshape(1, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        finally:
            binding.destroy(handle)

        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        assert int(out["action_id"][0]) == 357
        assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.5, abs=1e-6)
        assert float(out["speed_air_x_self"][0]) == pytest.approx(0.5, abs=1e-6)
