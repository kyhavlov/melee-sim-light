from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_dash_x4


def test_dash_x4_derivation_basic_latch() -> None:
    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_turn = 0x0012
    act_dash = 0x0014

    action_id = np.array(
        [
            act_wait,
            act_dash,  # entry from Wait -> x4=1
            act_dash,  # hold
            act_turn,
            act_dash,  # entry from Turn -> x4=0
            act_dash,  # hold
            act_dash,  # same-action restart at lower frame -> x4=1
            act_dash,
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([0, 0, 1, 0, 0, 1, 0, 1], dtype=np.int16)

    got = derive_dash_x4(action_id_u16=action_id, action_frame_i16=action_frame)
    want = np.array([0, 1, 1, 0, 0, 0, 1, 1], dtype=np.uint8)
    assert np.array_equal(got, want)


def test_dash_x4_derivation_is_prefix_invariant() -> None:
    act_wait = 0x000E
    act_turn = 0x0012
    act_dash = 0x0014

    action_id = np.array(
        [
            act_wait,
            act_dash,
            act_dash,
            act_turn,
            act_dash,
            act_dash,
            act_wait,
            act_dash,
            act_dash,
            act_dash,
            act_turn,
            act_dash,
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0], dtype=np.int16)

    full = derive_dash_x4(action_id_u16=action_id, action_frame_i16=action_frame)
    for k in (1, 2, 3, 5, 7, 9, 11, int(action_id.size)):
        got = derive_dash_x4(action_id_u16=action_id[:k], action_frame_i16=action_frame[:k])
        assert np.array_equal(got, full[:k])

