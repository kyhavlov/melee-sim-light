from __future__ import annotations

import numpy as np

from tools.eval.dataset import SEED_DTYPE


CHAR_FOX = 1
STAGE_FD = 32
MAX_PLAYERS = 4
MAX_ITEMS = 15


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
        ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
        ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_victim_port", ("u1", (MAX_PLAYERS,))),
        ("throw_pending_hit_idx", ("u1", (MAX_PLAYERS,))),
        ("attached_victim_port", ("u1", (MAX_PLAYERS,))),
    ],
    align=False,
)


def _seed_base() -> np.ndarray:
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
    return seed


def _reseed_and_read_counter(seed: np.ndarray) -> int:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    internals_stride = int(sizes["internals"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert internals_stride == INTERNALS_DTYPE.itemsize

    seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
    out_int = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_write_internals(handle, out_int)
    finally:
        msl_binding.destroy(handle)

    v = out_int.view(INTERNALS_DTYPE).reshape((1,))[0]
    return int(v["instance_id_counter"])


def test_instance_id_counter_reseed_is_max_plus_one_and_never_zero() -> None:
    # max(seed instance_id) = 5 => counter = 6
    seed = _seed_base()
    seed["instance_id"][0, 0] = np.uint16(5)
    assert _reseed_and_read_counter(seed) == 6

    # max across items should contribute too.
    seed2 = _seed_base()
    seed2["items"][0, 0]["exists"] = np.uint8(1)
    seed2["items"][0, 0]["instance_id"] = np.uint16(9)
    assert _reseed_and_read_counter(seed2) == 10

    # max=0 => counter = 1 (skip 0)
    seed3 = _seed_base()
    assert _reseed_and_read_counter(seed3) == 1


def test_instance_id_counter_reseed_wraps_u16_and_skips_zero() -> None:
    # max=0xFFFF => next wraps to 0 then corrected to 1.
    seed = _seed_base()
    seed["instance_id"][0, 0] = np.uint16(0xFFFF)
    assert _reseed_and_read_counter(seed) == 1

    seed2 = _seed_base()
    seed2["items"][0, 0]["exists"] = np.uint8(1)
    seed2["items"][0, 0]["instance_id"] = np.uint16(0xFFFF)
    assert _reseed_and_read_counter(seed2) == 1
