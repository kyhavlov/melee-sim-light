from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_GUARD_REFLECT = 0x00B6
ACT_GUARD_OFF = 0x00B4
SM_WAIT1_0 = 0x0002
SM_GUARD_REFLECT = 0x00A3
CHAR_FOX = 1
STAGE_FINAL_DESTINATION = 2


def _mk_input_bytes() -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    return np.zeros((1, int(binding.sizes()["input"])), dtype=np.uint8)


def _run_seed_step(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=True, ucf_cardinals_1_0_enabled=True)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, _mk_input_bytes(), _mk_input_bytes())
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _guard_reflect_exit_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FINAL_DESTINATION)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_REFLECT)
    seed["animation_index"][0, 0] = np.uint32(SM_GUARD_REFLECT)
    seed["state_flags"][0, 0, 4] = np.uint8(0x80)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["anim_frame_f32"][0, 0] = np.float32(10.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    return seed


@pytest.mark.parametrize(
    ("timer", "expected_timer_bit"),
    [
        (0, 0x00),
        (1, 0x00),
        (2, 0x80),
        (3, 0x80),
    ],
)
def test_guard_reflect_exit_uses_post_anim_timer_bits(timer: int, expected_timer_bit: int) -> None:
    # GuardReflect_Anim decrements fp->guardReflect.x1C, then exits when the post-decrement
    # value is zero. The replay-visible state_flags byte4 bit7 stores "timer > 1" at frame end,
    # so a seeded timer of 1 must publish a cleared post-frame bit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_Anim
    seed = _guard_reflect_exit_seed()
    seed["guard_reflect_timer_x14"][0, 0] = np.uint8(timer)
    seed["guard_reflect_timer_x18"][0, 0] = np.uint8(timer)
    seed["state_flags"][0, 0, 4] = np.uint8(0x80 if timer > 1 else 0x00)

    out = _run_seed_step(seed)
    assert int(out["action_id"][0]) == ACT_GUARD_OFF
    assert int(out["state_flags"][0, 4]) & 0x80 == expected_timer_bit
