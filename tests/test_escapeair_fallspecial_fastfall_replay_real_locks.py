from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_FALL_SPECIAL = 0x0023
ACT_FX_SPECIAL_HI_FALL = 0x0166
SM_WAIT1_0 = 0x0002
SM_FALL_SPECIAL = 0x001A
CHAR_FOX = 1
STAGE_FINAL_DESTINATION = 2


def _zero_input_bytes() -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    return np.zeros((1, int(binding.sizes()["input"])), dtype=np.uint8)


def _step_synthetic_seed_once(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=True, ucf_cardinals_1_0_enabled=True)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, _zero_input_bytes(), _zero_input_bytes())
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def test_spacie_specialhi_fall_source_does_not_inherit_xc0_overlay() -> None:
    # Adjacent source-callsite negative: Fox SpecialHiFall also exits through ftCo_80096900, but
    # its callsites pass arg1=1. Reconstruct xC from the data-backed fx-kind overlay, not from raw
    # SpecialHi ancestry. The seed starts just above Fox terminal velocity; xC=1 clamps after
    # gravity to -terminal_vel, while xC=0 would allow the fast-fall terminal lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    p = 0
    seed["stage_id"][0] = np.uint32(STAGE_FINAL_DESTINATION)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, p] = np.uint16(ACT_FALL_SPECIAL)
    seed["animation_index"][0, p] = np.uint32(SM_FALL_SPECIAL)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_FX_SPECIAL_HI_FALL)
    seed["action_frame"][0, p] = np.int16(4)
    seed["anim_frame_f32"][0, p] = np.float32(4.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["pos_y"][0, p] = np.float32(30.0)
    seed["speed_y_self"][0, p] = np.float32(-2.7)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["facing"][0, :2] = np.uint8(1)

    out = _step_synthetic_seed_once(seed)

    assert int(out["action_id"][p]) == ACT_FALL_SPECIAL
    assert float(out["speed_y_self"][p]) == pytest.approx(-2.8, abs=1e-6)
