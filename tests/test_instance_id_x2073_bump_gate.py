from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Fox/Falco down special (SpecialLw / Shine) start (GALE01): 360.
# refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c (commented numeric ids)
ACT_FX_SPECIAL_LW_START = 0x0168

BUTTON_B = 0x0200

CHAR_FOX = 1
STAGE_FD = 32


def test_instance_id_x2073_seed_can_suppress_ft_800895E0_bump_when_flags_match() -> None:
    # Unit regression: if fp+0x2073 matches the entering motion state's flags_low byte,
    # ft_800895E0 should not bump fp->x2088.
    #
    # Decomp anchor:
    # - refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz flags_low; compare vs fp+0x2073)
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["frame_id"][0] = np.int32(0)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)

    # Start on ground in Wait with a stable instance_id.
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["instance_id"][0, 0] = np.uint16(100)

    # Seed fp+0x2073 to match (u8)x4_flags for SpecialLwStart.
    # Current extracted tables map this to 20 (see data/attack_id/move_id/fox.bin).
    seed["instance_id_x2073"][0, 0] = np.uint8(20)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
    inp_view["p"]["main_y"][0, 0] = np.int8(-127)  # hard down to request SpecialLw

    out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
    finally:
        msl_binding.destroy(handle)

    out = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_LW_START
    assert int(out["instance_id"][0]) == 100

