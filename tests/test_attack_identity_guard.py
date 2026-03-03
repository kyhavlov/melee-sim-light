from __future__ import annotations

import numpy as np

from tools.eval.dataset import SEED_DTYPE


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

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
        ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
    ],
    align=False,
)


def test_attack_identity_guard_prevents_bump_on_anim_timebase_restart() -> None:
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

    # Seed into Wait but intentionally set an inconsistent attack identity. A pure animation
    # timebase restart (without action_id change) must not "fix" attack_id/attack_instance.
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, :2] = np.int16(0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["attack_id"][0, 0] = np.uint16(2)
    seed["attack_instance"][0, 0] = np.uint16(7)

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

    assert int(before["attack_id"][0]) == 2
    assert int(before["attack_instance"][0]) == 7
    assert int(before["attack_identity_last_action_id"][0]) == ACT_WAIT

    assert int(after["attack_id"][0]) == 2
    assert int(after["attack_instance"][0]) == 7
    assert int(after["attack_identity_last_action_id"][0]) == ACT_WAIT


def test_attack_identity_restart_does_not_bump_attack_instance_when_move_id_is_default() -> None:
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

    # If a pure anim-timebase restart accidentally invoked ft_800890D0, it would bump x206C when
    # move_id==FtMoveId_Default (1). Ensure this does not happen.
    # refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
    seed["attack_id"][0, 0] = np.uint16(1)
    seed["attack_instance"][0, 0] = np.uint16(7)

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_int = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_write_internals(handle, out_int)
        before = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()

        # Force a timebase reset without changing action_id (must not update attack identity).
        msl_binding.debug_force_anim_timebase_enter(handle, 0, 0, 0.0, 1.0)

        msl_binding.debug_write_internals(handle, out_int)
        after = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    assert int(before["attack_id"][0]) == 1
    assert int(before["attack_instance"][0]) == 7
    assert int(after["attack_id"][0]) == 1
    assert int(after["attack_instance"][0]) == 7
