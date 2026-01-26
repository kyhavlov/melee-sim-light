from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_FALL = 0x001D

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`
SM_WAIT1_0 = 2
SM_FALL = 20

CHAR_FOX = 1
STAGE_FD = 32


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _fox_grav() -> float:
    d = json.loads(Path("data/characters/fox.json").read_text())
    return float(d["grav"])


def test_physics_integrates_knockback_velocity_into_position_but_not_gravity() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["ground_id"][0, :2] = np.uint16(0)

    # P0: airborne and far from collision, with only knockback velocity set.
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(2000.0)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_x_attack"][0, 0] = np.float32(3.25)
    seed["speed_y_attack"][0, 0] = np.float32(-1.75)

    # P1: stable grounded idle.
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    # Position integrates self_vel + knockback_vel.
    assert np.isclose(float(out["pos_x"][0]), 3.25, atol=1e-6)
    assert np.isclose(float(out["pos_y"][0]), 2000.0 - 1.75, atol=1e-6)

    # Gravity updates only self velocity; knockback velocity is not modified by physics_integrate.
    assert np.isclose(float(out["speed_y_attack"][0]), -1.75, atol=1e-6)
    assert np.isclose(float(out["speed_y_self"][0]), -_fox_grav(), atol=1e-6)

