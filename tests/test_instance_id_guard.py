from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_THROW_LW = 0x00DE
ACT_THROWN_LW = 0x00F2

# Fox/Falco SpecialN action ids (GALE01):
# refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable (ftFx_MS_SpecialNStart=341)
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159
ACT_FX_SPECIAL_AIR_N_END = 0x015A
MSID_FX_SPECIAL_AIR_N_LOOP = 299

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
        ("entry_end_fall_lock", ("u1", (MAX_PLAYERS,))),
        ("attack_id", ("<u2", (MAX_PLAYERS,))),
        ("attack_instance", ("<u2", (MAX_PLAYERS,))),
        ("attack_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_x2073", ("u1", (MAX_PLAYERS,))),
        ("instance_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
        ("instance_id_counter", "<u2"),
        ("item_spawn_id_counter", "<u4"),
        ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_victim_port", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_hit_idx", ("u1", (MAX_PLAYERS,))),
        ("attached_victim_port", ("u1", (MAX_PLAYERS,))),
        ("dead_up_fall_offset_y", ("<f4", (MAX_PLAYERS,))),
        ("dead_up_fall_vel_y", ("<f4", (MAX_PLAYERS,))),
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


def test_attached_victim_port_reseed_uses_lowest_victim_port_deterministically() -> None:
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
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(CHAR_FOX)
    seed["attack_ratio"][0, :4] = np.float32(1.0)
    seed["defense_ratio"][0, :4] = np.float32(1.0)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)

    seed["action_id"][0, 0] = np.uint16(ACT_THROW_LW)
    seed["action_id"][0, 1] = np.uint16(ACT_THROWN_LW)
    seed["action_id"][0, 2] = np.uint16(ACT_THROWN_LW)
    seed["action_id"][0, 3] = np.uint16(ACT_WAIT)
    seed["grab_owner_port"][0, 1] = np.uint8(0)
    seed["grab_owner_port"][0, 2] = np.uint8(0)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_int = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_write_internals(handle, out_int)
        internals = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    assert int(internals["attached_victim_port"][0]) == 1
    assert int(internals["attached_victim_port"][1]) == 0xFF
    assert int(internals["attached_victim_port"][2]) == 0xFF


def test_blaster_loop_restart_requires_hidden_latch_not_instance_override_only() -> None:
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

    # The generic motion-entry instance override lane is not sufficient proof of the hidden
    # isBlasterLoop latch. Landing/entry rows can also use that lane, so a direct terminal Loop seed
    # with no live latch must enter End even when the override is present.
    # Decomp:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
    #   (when mv.fx.SpecialN.isBlasterLoop is true, sets fp->x21EC=ftFx_SpecialN_OnChangeAction and calls
    #    Fighter_ChangeMotionState to ftFx_MS_SpecialAirNLoop again)
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_OnChangeAction (calls ft_80089824)
    seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_AIR_N_LOOP)
    seed["action_frame"][0, 0] = np.int16(15)
    seed["anim_frame_f32"][0, 0] = np.float32(15.0)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(MSID_FX_SPECIAL_AIR_N_LOOP)

    # Keep P1 inert so this test isolates P0's SpecialAirNLoop terminal path.
    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
    seed["on_ground"][0, 1] = np.uint8(1)

    seed["instance_id"][0, 0] = np.uint16(100)
    seed["motion_entry_instance_id_override_u16"][0, 0] = np.uint16(102)
    # This used to be misinterpreted as Blaster-loop latch provenance. It is only an instance-order
    # seed lane and must not decide Loop vs End.
    seed["instance_id_x2073"][0, 0] = np.uint8(16)
    seed["on_ground"][0, 0] = np.uint8(0)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)

    out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_cmp)
    finally:
        msl_binding.destroy(handle)

    cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_AIR_N_END
