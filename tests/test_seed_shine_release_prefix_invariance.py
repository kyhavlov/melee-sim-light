from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_shine_release_state


def test_shine_release_derivation_basic_tick_and_latch() -> None:
    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c
    act_wait = 0x000E
    act_lw_start = 0x0168
    act_lw_loop = 0x0169
    act_lw_end = 0x016B
    act_lw_turn = 0x016C
    button_b = 0x0200

    action_id = np.array(
        [
            act_wait,
            act_lw_start,
            act_lw_start,
            act_lw_loop,
            act_lw_loop,
            act_lw_turn,
            act_lw_turn,
            act_lw_loop,
            act_lw_end,
            act_wait,
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([0, 0, 1, 0, 1, 0, 1, 0, 0, 0], dtype=np.int16)
    held = np.array(
        [0, button_b, button_b, button_b, 0, 0, 0, 0, 0, 0],
        dtype=np.uint16,
    )
    # Hitlag frame at i=5 freezes anim callbacks (no release tick that frame).
    hitlag = np.array([0, 0, 0, 0, 0, 3, 0, 0, 0, 0], dtype=np.uint16)
    release_init = np.array([5] * action_id.size, dtype=np.uint8)

    lag, is_release = derive_shine_release_state(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        buttons_held_u16=held,
        hitlag_u16=hitlag,
        release_lag_init_u8=release_init,
        button_mask_b=button_b,
    )

    want_lag = np.array([0, 5, 5, 4, 3, 3, 2, 1, 1, 0], dtype=np.uint8)
    want_is_release = np.array([0, 0, 0, 0, 1, 1, 1, 1, 1, 0], dtype=np.uint8)
    assert np.array_equal(lag, want_lag)
    assert np.array_equal(is_release, want_is_release)


def test_shine_release_derivation_is_prefix_invariant() -> None:
    action_id = np.array(
        [
            0x000E,
            0x0168,
            0x0168,
            0x0169,
            0x0169,
            0x016A,
            0x016C,
            0x0169,
            0x016B,
            0x000E,
            0x016D,
            0x016E,
            0x0171,
            0x0170,
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.int16)
    held = np.array(
        [0, 0x0200, 0x0200, 0x0200, 0, 0, 0, 0, 0, 0, 0x0200, 0, 0, 0],
        dtype=np.uint16,
    )
    hitlag = np.array([0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    release_init = np.array([18] * action_id.size, dtype=np.uint8)

    full_lag, full_is_release = derive_shine_release_state(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        buttons_held_u16=held,
        hitlag_u16=hitlag,
        release_lag_init_u8=release_init,
    )
    for k in (1, 2, 3, 5, 7, 9, 11, int(action_id.size)):
        lag, is_release = derive_shine_release_state(
            action_id_u16=action_id[:k],
            action_frame_i16=action_frame[:k],
            buttons_held_u16=held[:k],
            hitlag_u16=hitlag[:k],
            release_lag_init_u8=release_init[:k],
        )
        assert np.array_equal(lag, full_lag[:k])
        assert np.array_equal(is_release, full_is_release[:k])
