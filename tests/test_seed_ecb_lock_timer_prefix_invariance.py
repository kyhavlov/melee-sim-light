from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_ecb_lock_timer


def test_ecb_lock_timer_derivation_basic_countdown() -> None:
    # Post-frame grounding history with two grounded->air transitions.
    on_ground = np.array(
        [
            1,  # grounded
            1,  # grounded
            0,  # takeoff frame -> lock set then same-frame tick => 9
            0,  # 8
            0,  # 7
            1,  # landing clears
            0,  # second takeoff -> 9
            0,  # 8
        ],
        dtype=np.uint8,
    )
    action_id = np.array(
        [
            0x000E,
            0x000E,
            0x0019,  # JumpF entry
            0x0019,
            0x0019,
            0x000E,
            0x001B,  # JumpAerialF entry
            0x001B,
        ],
        dtype=np.uint16,
    )
    got = derive_ecb_lock_timer(
        on_ground_u8=on_ground,
        action_id_u16=action_id,
        lock_frames_ground_to_air=10,
    )
    want = np.array([0, 0, 9, 8, 7, 0, 9, 8], dtype=np.uint8)
    assert np.array_equal(got, want)


def test_ecb_lock_timer_derivation_is_prefix_invariant() -> None:
    on_ground = np.array([1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0], dtype=np.uint8)
    action_id = np.array([0x000E, 0x000E, 0x0019, 0x0019, 0x0019, 0x0019, 0x000E, 0x001B, 0x001B, 0x001B, 0x000E, 0x000E, 0x001A, 0x001A], dtype=np.uint16)
    full = derive_ecb_lock_timer(
        on_ground_u8=on_ground,
        action_id_u16=action_id,
        lock_frames_ground_to_air=10,
    )
    for k in (1, 2, 3, 4, 6, 8, 10, int(on_ground.size)):
        got = derive_ecb_lock_timer(
            on_ground_u8=on_ground[:k],
            action_id_u16=action_id[:k],
            lock_frames_ground_to_air=10,
        )
        assert np.array_equal(got, full[:k])
