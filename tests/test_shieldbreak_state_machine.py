from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
ACT_SHIELD_BREAK_DOWN_U = 0x00CF
ACT_SHIELD_BREAK_STAND_U = 0x00D1
ACT_FURAFURA = 0x00D3

SM_WAIT1_0 = 2
SM_SHIELD_BREAK_DOWN_U = 288
SM_SHIELD_BREAK_STAND_U = 290
SM_FURAFURA = 205

CHAR_FOX = 1
STAGE_FD = 32


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_grounded(action: int, submotion: int, frame: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["shield_hp"][0, :2] = np.float32(_common_attr("start_shield_health"))
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(action)
    seed["animation_index"][0, 0] = np.uint32(submotion)
    seed["action_frame"][0, 0] = np.int16(frame)
    seed["anim_frame_f32"][0, 0] = np.float32(float(frame))
    seed["hurtbox_state"][0, 0] = np.uint8(2)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


def _step_once(seed: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _step_handle(handle, prev_inp: np.ndarray, inp: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.step_input(handle, prev_inp, inp)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def test_shieldbreak_down_anim_end_enters_stand() -> None:
    # ShieldBreakDown_Anim exits through ftCo_80098F3C, preserving the U/D side.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_ShieldBreakDown_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
    out = _step_once(_seed_grounded(ACT_SHIELD_BREAK_DOWN_U, SM_SHIELD_BREAK_DOWN_U, 1000))
    assert int(out["action_id"][0]) == ACT_SHIELD_BREAK_STAND_U
    assert int(out["animation_index"][0]) == SM_SHIELD_BREAK_STAND_U
    assert int(out["hurtbox_state"][0]) == 2


def test_shieldbreak_stand_anim_end_enters_furafura_with_timer() -> None:
    # ShieldBreakStand_Anim exits through ftCo_80099010, which enters Furafura and initializes
    # fp->grab_timer from p_ftCommonData->{x2F8,x2FC}.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_ShieldBreakStand_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
    out = _step_once(_seed_grounded(ACT_SHIELD_BREAK_STAND_U, SM_SHIELD_BREAK_STAND_U, 1000))
    assert int(out["action_id"][0]) == ACT_FURAFURA
    assert int(out["animation_index"][0]) == SM_FURAFURA
    assert int(out["hurtbox_state"][0]) == 0
    expected_hp = _common_attr("shield_break_reset_health") + _common_attr("shield_recharge_per_frame")
    assert float(out["shield_hp"][0]) == pytest.approx(expected_hp)


def test_shieldbreak_stand_furafura_entry_timer_boundary() -> None:
    # There is no compare/debug ABI field for fp->grab_timer, so lock the entry timer through the
    # visible Furafura -> Wait boundary. At 400%, ftCo_80099010 clamps the percent term to 0 and
    # initializes the timer to p_ftCommonData->x2FC; Furafura_Anim subtracts x300 once per frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::{ftCo_80099010,ftCo_Furafura_Anim}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    expected_timer = _common_attr("furafura_timer_base")
    decrement = _common_attr("furafura_timer_decrement")
    assert decrement > 0.0
    expected_steps = int(round(expected_timer / decrement))
    assert expected_timer == pytest.approx(float(expected_steps) * decrement)

    seed = _seed_grounded(ACT_SHIELD_BREAK_STAND_U, SM_SHIELD_BREAK_STAND_U, 1000)
    seed["percent"][0, 0] = np.float32(_common_attr("furafura_timer_percent_base"))

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        out = _step_handle(handle, prev_inp, inp)
        assert int(out["action_id"][0]) == ACT_FURAFURA

        for _ in range(expected_steps - 1):
            out = _step_handle(handle, inp, inp)
        assert int(out["action_id"][0]) == ACT_FURAFURA

        out = _step_handle(handle, inp, inp)
        assert int(out["action_id"][0]) == ACT_WAIT
        assert int(out["animation_index"][0]) == SM_WAIT1_0
    finally:
        msl_binding.destroy(handle)


def test_furafura_timer_expiry_enters_wait() -> None:
    # Furafura_Anim decrements fp->grab_timer and exits through ft_8008A2BC when it reaches zero.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_Furafura_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    seed = _seed_grounded(ACT_FURAFURA, SM_FURAFURA, 10)
    seed["hurtbox_state"][0, 0] = np.uint8(0)
    seed["capture_grab_timer_f32"][0, 0] = np.float32(1.0)
    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["animation_index"][0]) == SM_WAIT1_0
