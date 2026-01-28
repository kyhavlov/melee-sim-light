from __future__ import annotations

import numpy as np

from tools.slippi.combo_history import derive_combo_seed_fields


def test_combo_history_prefix_invariance() -> None:
    # Strict prefix invariance: derive(prefix) == derive(full)[:k] for many k.
    n_frames = 12
    num_players = 2
    src_ports = [1, 2]

    hitlag = np.zeros((n_frames, 4), dtype=np.uint16)
    # New hit on victim P2 at t=2 (hitlag rising edge).
    hitlag[2, 1] = np.uint16(3)
    hitlag[3, 1] = np.uint16(2)
    hitlag[4, 1] = np.uint16(1)

    # state_flags[..., 3] bit0x02 = isHitstun
    state_flags = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    state_flags[2:7, 1, 3] = np.uint8(0x02)

    instance_id = np.zeros((n_frames, 4), dtype=np.uint16)
    instance_id[:, 0] = np.uint16(100)
    instance_id[:, 1] = np.uint16(200)

    last_hit_by = np.full((n_frames, 4), 0xFF, dtype=np.uint8)
    last_hit_by[2, 1] = np.uint8(0)  # victim P2 last hit by port0==0 (P1)

    full_port, full_iid, full_timer = derive_combo_seed_fields(
        num_players=num_players,
        src_ports=src_ports,
        hitlag=hitlag,
        state_flags=state_flags,
        instance_id=instance_id,
        last_hit_by=last_hit_by,
        data_root="data",
    )

    # Sanity: combo victim pointer for attacker (slot0) is set on first hit.
    assert int(full_port[2, 0]) == 1
    assert int(full_iid[2, 0]) == 200

    ks = [1, 2, 3, 4, 5, 7, n_frames - 1, n_frames]
    ks = [k for k in ks if 1 <= k <= n_frames]
    for k in ks:
        pref_port, pref_iid, pref_timer = derive_combo_seed_fields(
            num_players=num_players,
            src_ports=src_ports,
            hitlag=hitlag[:k].copy(),
            state_flags=state_flags[:k].copy(),
            instance_id=instance_id[:k].copy(),
            last_hit_by=last_hit_by[:k].copy(),
            data_root="data",
        )
        assert np.array_equal(pref_port, full_port[:k])
        assert np.array_equal(pref_iid, full_iid[:k])
        assert np.array_equal(pref_timer, full_timer[:k])

