from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


STAGE_FD = 32
CHAR_FOX = 1
ACT_WAIT = 14
ACT_DEAD_UP_FALL = 6
ACT_DEAD_UP_FALL_HIT_CAMERA = 7
SM_WAIT = 2
SM_DAMAGE_FALL = 29
SM_DEAD_UP_FALL_HIT_CAMERA = 0


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(40.0)
    seed["on_ground"][0, :2] = np.uint8(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)

    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT)
    return seed


def _step_once(seed: np.ndarray) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_deadupfall_countdown_enters_hitcamera_without_future_position_bridge() -> None:
    # ftCo_DeadUpFall_Anim case 1 transitions through ftCo_800D481C when x40 expires.
    # This uses only the causal x520/x528 timer owner, not a replay-next position delta.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D481C}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["match_flow_timer"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(out["animation_index"][0]) == SM_DEAD_UP_FALL_HIT_CAMERA


def test_deadupfall_hitcamera_hold_expiry_applies_phase3_fall_velocity() -> None:
    # ftCo_DeadUpFall_Anim case 2 writes self_vel.y=x550, then ftCo_DeadUpFall_Phys case 3
    # immediately applies ftCommon_Fall with x554/x558 before Fighter_procUpdate position integration.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_DeadUpFall_Phys}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(76)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert float(out["speed_y_self"][0]) == pytest.approx(0.8, abs=1e-6)
    assert float(out["pos_y"][0]) == pytest.approx(40.8, abs=1e-6)


def test_deadupfall_hitcamera_phase3_expiry_loses_stock_once() -> None:
    # ftCo_DeadUpFall_Anim case 3 calls ftCo_800D34E0 when the phase-3 timer expires.
    # Lock the boundary at prev_t == x534 + 1 so the stock loss does not drift by one frame.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_DeadUpFall_Anim,ftCo_800D34E0}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_UP_FALL_HIT_CAMERA)
    seed["animation_index"][0, 0] = np.uint32(SM_DEAD_UP_FALL_HIT_CAMERA)
    seed["match_flow_timer"][0, 0] = np.uint8(36)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_UP_FALL_HIT_CAMERA
    assert int(out["stocks"][0]) == 3
