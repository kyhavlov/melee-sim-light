from __future__ import annotations

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Fox/Falco SpecialN action ids (GALE01):
# refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable (ftFx_MS_SpecialNStart=341)
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159

BUTTON_B = 0x0200

CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4

INTERNALS_DTYPE = np.dtype(
    [
        ("tilt_timer_x", ("u1", (MAX_PLAYERS,))),
        ("turn_frames_to_turn", ("u1", (MAX_PLAYERS,))),
        ("turn_has_turned", ("u1", (MAX_PLAYERS,))),
        ("guard_reflect_timer_x14", ("u1", (MAX_PLAYERS,))),
        ("attack_id", ("<u2", (MAX_PLAYERS,))),
        ("attack_instance", ("<u2", (MAX_PLAYERS,))),
        ("attack_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_x2073", ("u1", (MAX_PLAYERS,))),
        ("instance_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_counter", "<u2"),
    ],
    align=False,
)


def test_instance_id_guard_prevents_bump_on_anim_timebase_restart() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    internals_stride = int(sizes["internals"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert internals_stride == INTERNALS_DTYPE.itemsize

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

    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    seed["instance_id"][0, 0] = np.uint16(111)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_int = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_write_internals(handle, out_int)
        before = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()

        # Force a timebase reset without changing action_id.
        msl_binding.debug_force_anim_timebase_enter(handle, 0, 0, 0.0, 1.0)

        msl_binding.debug_write_internals(handle, out_int)
        after = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    assert int(before["instance_id"][0]) == 111
    assert int(before["instance_identity_last_action_id"][0]) == ACT_WAIT
    assert int(after["instance_id"][0]) == 111
    assert int(after["instance_identity_last_action_id"][0]) == ACT_WAIT


def test_instance_id_bumps_on_action_entry_specialn() -> None:
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
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    seed["instance_id"][0, 0] = np.uint16(100)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)

    out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
    finally:
        msl_binding.destroy(handle)

    cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_N_START
    assert int(cmp0["instance_id"][0]) != 100


def test_instance_id_bumps_on_blaster_loop_restart_same_action_id() -> None:
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

    # Seed directly into SpecialAirNLoop at an end-frame so the sim restarts the loop when B is held.
    # Decomp:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
    #   (when mv.fx.SpecialN.isBlasterLoop is true, sets fp->x21EC=ftFx_SpecialN_OnChangeAction and calls
    #    Fighter_ChangeMotionState to ftFx_MS_SpecialAirNLoop again)
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction (calls ft_80089824)
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_AIR_N_LOOP)
    seed["action_frame"][0, 0] = np.int16(999)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    # Prevent laser spawn noise: items_update skips per-frame spawns when animation_index is -1.
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)

    seed["instance_id"][0, 0] = np.uint16(100)
    seed["on_ground"][0, 0] = np.uint8(0)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp_view = inp.view(INPUT_DTYPE).reshape((1,))
    inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)

    out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
    finally:
        msl_binding.destroy(handle)

    cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_AIR_N_LOOP
    # In GALE01, the loop restart runs Fighter_ChangeMotionState (ft_800895E0) and then x21EC
    # OnChangeAction (ft_80089824), so fp->x2088 bumps twice on this frame.
    assert int(cmp0["instance_id"][0]) == 102
