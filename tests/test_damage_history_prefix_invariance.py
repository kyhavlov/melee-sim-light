from __future__ import annotations

import numpy as np

from tools.slippi.damage_history import derive_damage_time_since_hit_x18ac


def _python_damage_time_since_hit_x18ac(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    state_flags_u8: np.ndarray,
) -> np.ndarray:
    del action_id_u16
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    flags = np.asarray(state_flags_u8, dtype=np.uint8)
    if flags.ndim != 2 or flags.shape[1] < 4:
        raise ValueError("state_flags_u8 must have shape [n, >=4]")
    if int(hitlag.shape[0]) != int(hitstun.shape[0]) or int(hitlag.shape[0]) != int(flags.shape[0]):
        raise ValueError("damage-history input arrays must have matching frame counts")

    out = np.full((int(hitlag.shape[0]),), -1, dtype=np.int16)
    timer = -1
    prev_hitlag = 0
    prev_hitstun = 0
    for fi in range(int(hitlag.shape[0])):
        hl = int(hitlag[fi])
        hs = int(hitstun[fi])
        in_hitstun = (int(flags[fi, 3]) & 0x02) != 0
        fresh_damage = (prev_hitlag == 0 and hl > 0 and (hs > 0 or in_hitstun)) or (
            hs > prev_hitstun + 1
        )
        if fresh_damage:
            timer = 0
        elif timer >= 0 and hl == 0:
            timer = min(timer + 1, np.iinfo(np.int16).max)
        out[fi] = np.int16(timer)
        prev_hitlag = hl
        prev_hitstun = hs
    return out


def test_damage_time_since_hit_x18ac_prefix_invariance() -> None:
    n = 8
    action_id = np.zeros(n, dtype=np.uint16)
    hitlag = np.array([0, 4, 3, 0, 0, 0, 2, 0], dtype=np.uint16)
    hitstun = np.array([0, 12, 12, 11, 10, 9, 16, 16], dtype=np.uint16)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    state_flags[1:, 3] = np.uint8(0x02)

    full = derive_damage_time_since_hit_x18ac(
        action_id_u16=action_id,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=state_flags,
    )

    # Fresh hit at frame 1 resets to 0, hitlag freezes it, non-hitlag frames increment, and the
    # second hit at frame 6 resets it again.
    np.testing.assert_array_equal(
        full,
        _python_damage_time_since_hit_x18ac(
            action_id_u16=action_id,
            hitlag_u16=hitlag,
            hitstun_u16=hitstun,
            state_flags_u8=state_flags,
        ),
    )
    assert full.tolist() == [-1, 0, 0, 1, 2, 3, 0, 1]
    assert full.dtype == np.dtype(np.int16)

    for cutoff in range(1, n + 1):
        pref = derive_damage_time_since_hit_x18ac(
            action_id_u16=action_id[:cutoff],
            hitlag_u16=hitlag[:cutoff],
            hitstun_u16=hitstun[:cutoff],
            state_flags_u8=state_flags[:cutoff],
        )
        assert pref.tolist() == full[:cutoff].tolist()


def test_damage_time_since_hit_x18ac_native_matches_python_oracle() -> None:
    n = 97
    idx = np.arange(n, dtype=np.uint16)
    action_id = (idx * np.uint16(7)).astype(np.uint16)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.maximum(0, 80 - np.arange(n, dtype=np.int32)).astype(np.uint16)
    state_flags = np.zeros((n, 6), dtype=np.uint8)

    hitlag[[2, 3, 25, 54, 55, 56, 90]] = np.array([3, 2, 4, 5, 4, 3, 2], dtype=np.uint16)
    hitstun[2:] = np.maximum(hitstun[2:], np.uint16(12))
    hitstun[25:] = np.maximum(hitstun[25:], np.uint16(42))
    hitstun[54:] = np.maximum(hitstun[54:], np.uint16(68))
    state_flags[2:8, 3] = np.uint8(0x02)
    state_flags[25:31, 3] = np.uint8(0x02)
    state_flags[54:61, 3] = np.uint8(0x02)

    actual = derive_damage_time_since_hit_x18ac(
        action_id_u16=action_id,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=state_flags,
    )
    expected = _python_damage_time_since_hit_x18ac(
        action_id_u16=action_id,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=state_flags,
    )
    np.testing.assert_array_equal(actual, expected)
