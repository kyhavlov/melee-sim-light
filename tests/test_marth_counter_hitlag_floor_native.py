from __future__ import annotations

import numpy as np

import msl_binding


def _marth_counter_hitlag_floor_oracle(
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    state_flags_u8: np.ndarray,
) -> np.ndarray:
    act_counter_ground = np.uint16(369)
    act_counter_air = np.uint16(371)
    live = (
        (char_id_u8 == np.uint8(18))
        & ((action_id_u16 == act_counter_ground) | (action_id_u16 == act_counter_air))
        & ((state_flags_u8[:, 2] & np.uint8(0x80)) != 0)
    )
    out = np.zeros(action_id_u16.shape[0], dtype=np.uint8)
    active_floor = np.uint8(0)
    prev_live = False
    prev_action = np.uint16(0)
    for i in range(action_id_u16.shape[0]):
        if not bool(live[i]):
            active_floor = np.uint8(0)
            prev_live = False
            prev_action = action_id_u16[i]
            continue
        action = action_id_u16[i]
        swapped = prev_live and (
            (prev_action == act_counter_ground and action == act_counter_air)
            or (prev_action == act_counter_air and action == act_counter_ground)
        )
        if swapped:
            active_floor = np.uint8(0)
        elif not prev_live:
            active_floor = np.uint8(1)
        out[i] = active_floor
        prev_live = True
        prev_action = action
    return out


def test_native_marth_counter_hitlag_floor_matches_oracle() -> None:
    action = np.array(
        [
            14,
            369,
            369,
            371,
            371,
            14,
            371,
            371,
            369,
            369,
            369,
            371,
            371,
            371,
        ],
        dtype=np.uint16,
    )
    char_id = np.array(
        [
            18,
            18,
            18,
            18,
            18,
            18,
            18,
            18,
            18,
            18,
            22,
            22,
            18,
            18,
        ],
        dtype=np.uint8,
    )
    state_flags = np.zeros((action.shape[0], 5), dtype=np.uint8)
    state_flags[:, 2] = np.array(
        [
            0x80,
            0x80,
            0x80,
            0x80,
            0x80,
            0x00,
            0x80,
            0x80,
            0x80,
            0x00,
            0x80,
            0x80,
            0x00,
            0x80,
        ],
        dtype=np.uint8,
    )

    expected = _marth_counter_hitlag_floor_oracle(char_id, action, state_flags)
    got = msl_binding.derive_marth_counter_hitlag_floor_active(char_id, action, state_flags)

    np.testing.assert_array_equal(got, expected)
    np.testing.assert_array_equal(
        got,
        np.array([0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1], dtype=np.uint8),
    )


def test_native_marth_counter_hitlag_floor_rejects_short_state_flags() -> None:
    char_id = np.array([18], dtype=np.uint8)
    action = np.array([369], dtype=np.uint16)
    state_flags = np.zeros((1, 2), dtype=np.uint8)
    try:
        msl_binding.derive_marth_counter_hitlag_floor_active(char_id, action, state_flags)
    except ValueError as exc:
        assert "state_flags" in str(exc)
    else:
        raise AssertionError("expected short state_flags to be rejected")
