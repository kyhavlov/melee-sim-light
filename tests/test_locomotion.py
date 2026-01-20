from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_X = 0x0400

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_TURN = 0x0012
ACT_KNEEBEND = 0x0018
ACT_JUMPF = 0x0019
ACT_FALL = 0x001D
ACT_LANDING = 0x002A

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_TURN = 10
SM_KNEEBEND = 15
SM_JUMPF = 16
SM_FALL = 20

CHAR_FOX = 1
STAGE_FD = 32


def _fox_attr(name: str) -> float:
    import json
    from pathlib import Path

    fox = json.loads(Path("data/characters/fox.json").read_text())
    return float(fox[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


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
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


def _step_many(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray, n: int) -> list[np.ndarray]:
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
        outs: list[np.ndarray] = []

        msl_binding.reseed_seed(handle, seed_bytes)
        for _ in range(n):
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out)
            outs.append(out.view(COMPARE_DTYPE).reshape((1,))[0].copy())
        return outs
    finally:
        msl_binding.destroy(handle)


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["facing"][0, 0] = np.uint8(1)  # right
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["ground_id"][0, 0] = np.uint16(0)
    return seed


def test_kneebend_takeoff_enters_jumpf_and_consumes_jump() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Hold X and hold stick right.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 1
    assert int(out["on_ground"][0]) == 0
    expected_vy = np.float32(_fox_attr("jump_v_initial_velocity") - _fox_attr("grav"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_wait_press_jump_enters_kneebend() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT1_0)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Press X this frame.
    prev_view["p"]["buttons"][0, 0] = np.uint16(0)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_KNEEBEND
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 2
    assert int(out["on_ground"][0]) == 1


def test_kneebend_release_jump_short_hops() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_KNEEBEND)
    seed["action_frame"][0, 0] = np.int16(2)  # Fox jump_startup_frames=3, so +1 triggers takeoff.
    seed["animation_index"][0, 0] = np.uint32(SM_KNEEBEND)
    seed["jumps_left"][0, 0] = np.uint8(2)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    # Release X on takeoff frame.
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["buttons"][0, 0] = np.uint16(0)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_JUMPF
    assert int(out["action_frame"][0]) == 0
    expected_vy = np.float32(_fox_attr("hop_v_initial_velocity") - _fox_attr("grav"))
    assert np.isclose(out["speed_y_self"][0], expected_vy)


def test_air_jump_consumes_jump_and_enters_jump_aerial() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["speed_y_self"][0, 0] = np.float32(-1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["buttons"][0, 0] = np.uint16(0)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_X)
    cur_view["p"]["main_x"][0, 0] = np.int8(80)

    out = _step_once(seed, prev_inp, inp)
    # JumpAerialF
    assert int(out["action_id"][0]) == 0x001B
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 0


def test_landing_resets_jumps_and_enters_landing() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(1.0)
    seed["speed_y_self"][0, 0] = np.float32(-2.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_FALL)
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    out = _step_once(seed, prev_inp, inp)
    assert int(out["on_ground"][0]) == 1
    assert int(out["action_id"][0]) == ACT_LANDING
    assert int(out["action_frame"][0]) == 0
    assert int(out["jumps_left"][0]) == 2


def test_jump_end_enters_fall() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(0)
    # Keep the fighter airborne so collision doesn't force a Landing transition.
    seed["pos_y"][0, 0] = np.float32(20.0)
    seed["speed_y_self"][0, 0] = np.float32(1.0)
    seed["action_id"][0, 0] = np.uint16(ACT_JUMPF)
    seed["action_frame"][0, 0] = np.int16(120)
    seed["animation_index"][0, 0] = np.uint32(SM_JUMPF)
    seed["jumps_left"][0, 0] = np.uint8(1)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out = _step_once(seed, prev_inp, inp)
    assert int(out["action_id"][0]) == ACT_FALL
    assert int(out["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == SM_FALL


def test_turn_reseed_does_not_flip_immediately_when_action_frame_is_0() -> None:
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["facing"][0, 0] = np.uint8(1)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(1)
    seed["turn_has_turned"][0, 0] = np.uint8(0)

    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out0, out1, out2 = _step_many(seed, prev_inp, inp, 3)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["facing"][0]) == 1
    assert int(out1["action_id"][0]) == ACT_TURN
    assert int(out1["facing"][0]) == 0
    assert int(out2["action_id"][0]) == ACT_TURN
    assert int(out2["facing"][0]) == 0


def test_turn_seeded_has_turned_prevents_double_flip() -> None:
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["facing"][0, 0] = np.uint8(0)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(0)
    seed["turn_has_turned"][0, 0] = np.uint8(1)

    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)

    out0, out1 = _step_many(seed, prev_inp, inp, 2)
    assert int(out0["action_id"][0]) == ACT_TURN
    assert int(out0["facing"][0]) == 0
    assert int(out1["action_id"][0]) == ACT_TURN
    assert int(out1["facing"][0]) == 0
