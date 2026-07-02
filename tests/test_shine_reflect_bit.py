from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Fox/Falco Shine action ids (GALE01):
# refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
# (ftFx_MS_SpecialLwStart=360 .. ftFx_MS_SpecialAirLwTurn=369)
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_FX_SPECIAL_LW_END = 0x016B
ACT_FX_SPECIAL_LW_TURN = 0x016C

ACT_WAIT = 0x000E

BUTTON_B = 0x0200

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32

# Slippi state_flags byte0 (fp+0x2218) reflect-active bit.
STATE_FLAG_2218_REFLECT_ACTIVE = 0x10


def _char_attr(char_id: int, name: str) -> int:
    char_name = "fox" if int(char_id) == CHAR_FOX else "falco"
    data = json.loads(Path(f"data/characters/{char_name}.json").read_text())
    return int(data[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def test_shine_reflect_active_bit_is_set_in_loop() -> None:
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
        prev_inp_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        prev_inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        for cid in (CHAR_FOX, CHAR_FALCO):
            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["frame_id"][0] = np.int32(0)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, 0] = np.uint8(cid)
            seed["char_id"][0, 1] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_LOOP)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            seed["anim_frame_f32"][0, 0] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["state_flags"][0, 0, 0] = np.uint8(0x00)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_LOOP
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) != 0
    finally:
        msl_binding.destroy(handle)


def test_shine_reflect_active_bit_clears_on_release_to_end() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        for cid in (CHAR_FOX, CHAR_FALCO):
            rl = _char_attr(cid, "reflector_release_lag_frames")

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_LOOP)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            seed["anim_frame_f32"][0, 0] = np.float32(float(max(0, rl)))
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["state_flags"][0, 0, 0] = np.uint8(STATE_FLAG_2218_REFLECT_ACTIVE)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_END
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) == 0
    finally:
        msl_binding.destroy(handle)


def test_shine_reflect_active_bit_set_when_turn_transitions_to_loop() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        prev_inp_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        prev_inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        for cid in (CHAR_FOX, CHAR_FALCO):
            tf = _char_attr(cid, "reflector_turn_frames")
            assert tf > 0

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_TURN)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            seed["anim_frame_f32"][0, 0] = np.float32(float(tf))
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["state_flags"][0, 0, 0] = np.uint8(0x00)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_LOOP
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) != 0
    finally:
        msl_binding.destroy(handle)
