from __future__ import annotations

import numpy as np

from tools.slippi.damage_history import derive_damage_time_since_hit_x18ac


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
    assert full.tolist() == [-1, 0, 0, 1, 2, 3, 0, 1]

    for cutoff in range(1, n + 1):
        pref = derive_damage_time_since_hit_x18ac(
            action_id_u16=action_id[:cutoff],
            hitlag_u16=hitlag[:cutoff],
            hitstun_u16=hitstun[:cutoff],
            state_flags_u8=state_flags[:cutoff],
        )
        assert pref.tolist() == full[:cutoff].tolist()
