from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_DAMAGEFALL = 0x0026
ACT_DEAD_DOWN = 0x0000

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
SM_WAIT1_0 = 2
SM_DAMAGE_FALL = 29

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32


def _fd_blast_bottom() -> float:
    fd = json.loads(Path("data/stages/final_destination.json").read_text())
    blast = fd.get("blast_bounds_world", {})
    return float(blast["bottom"])


def test_damagefall_near_blast_bottom_does_not_false_positive_ko() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    blast_bottom = np.float32(_fd_blast_bottom())

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, 0] = np.uint8(4)
    seed["stocks"][0, 1] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)

    # P0: DamageFall just above the blast bottom with no downward velocity.
    # If we incorrectly apply common fall gravity pre-integration in DamageFall, pos_y can cross
    # `blast_bottom` on this step and trigger a false-positive KO.
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGEFALL)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(SM_DAMAGE_FALL)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["anim_frame_f32"][0, 0] = np.float32(0.0)
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_y"][0, 0] = blast_bottom + np.float32(0.10)
    seed["speed_air_x_self"][0, 0] = np.float32(0.0)
    seed["speed_y_self"][0, 0] = np.float32(0.0)
    seed["speed_x_attack"][0, 0] = np.float32(0.0)
    seed["speed_y_attack"][0, 0] = np.float32(0.0)

    # P1: stable grounded idle.
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["ground_id"][0, 1] = np.uint16(1)

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        o = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    assert float(o["pos_y"][0]) > float(blast_bottom)
    assert int(o["action_id"][0]) != ACT_DEAD_DOWN
    assert int(o["stocks"][0]) == 4
