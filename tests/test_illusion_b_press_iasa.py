from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Button masks: src/buttons.h (Melee/HSD PAD bits)
BUTTON_B = 0x0200

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
ACT_WAIT = 0x000E
ACT_FX_SPECIAL_S = 0x015C
ACT_FX_SPECIAL_S_END = 0x015D
ACT_FX_SPECIAL_AIR_S = 0x015F
ACT_FX_SPECIAL_AIR_S_END = 0x0160

# Submotion ids (GALE01): data/special_msids/fox.json
SM_WAIT1_0 = 2
SM_FX_SPECIAL_S_END = 303
SM_FX_SPECIAL_AIR_S_END = 306

CHAR_FOX = 1
STAGE_FD = 32


def _fox_attr(name: str) -> float:
    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _seed_base(*, p0_on_ground: bool) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0 if p0_on_ground else 10.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1 if p0_on_ground else 0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)

    # Default P2 to a stable grounded idle.
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


def _step_once(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> np.ndarray:
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
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_illusion_air_b_pressed_edge_enters_special_air_s_end_same_step() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base(p0_on_ground=False)
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_AIR_S)
    seed["action_frame"][0, 0] = np.int16(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_AIR_S_END
    assert int(out["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == SM_FX_SPECIAL_AIR_S_END
    # Enter sets self_vel.x to the data attr, but physics runs later in the frame and can apply
    # friction; just assert we got a positive non-zero speed in the expected range.
    exp = _fox_attr("illusion_air_end_vel_x")
    got = float(out["speed_air_x_self"][0])
    assert 0.0 < got <= exp
    assert float(out["speed_y_self"][0]) == 0.0


def test_illusion_ground_b_pressed_edge_enters_special_s_end_same_step() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base(p0_on_ground=True)
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_S)
    seed["action_frame"][0, 0] = np.int16(0)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_S_END
    assert int(out["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == SM_FX_SPECIAL_S_END
    exp = _fox_attr("illusion_ground_end_vel_x")
    got = float(out["speed_ground_x_self"][0])
    assert 0.0 < got <= exp
