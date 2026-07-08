from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.slippi.combo_history import derive_combo_push_timer_seed, derive_combo_seed_fields


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


def test_combo_seed_preserves_victim_on_hitstun_end_row() -> None:
    # Source order:
    # Fighter_8006A360 runs ftColl_800764DC before ftCo_8008F744 clears hitstun and writes x2098.
    # The post-frame row where hitstun first becomes clear must therefore still carry x2094.
    n_frames = 9
    num_players = 2
    src_ports = [1, 2]

    hitlag = np.zeros((n_frames, 4), dtype=np.uint16)
    hitlag[1, 1] = np.uint16(3)
    hitlag[2, 1] = np.uint16(2)
    hitlag[3, 1] = np.uint16(1)

    state_flags = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    state_flags[1:4, 1, 3] = np.uint8(0x02)

    instance_id = np.zeros((n_frames, 4), dtype=np.uint16)
    instance_id[:, 0] = np.uint16(100)
    instance_id[:, 1] = np.uint16(200)

    last_hit_by = np.full((n_frames, 4), 0xFF, dtype=np.uint8)
    last_hit_by[1, 1] = np.uint8(0)

    port, iid, timer = derive_combo_seed_fields(
        num_players=num_players,
        src_ports=src_ports,
        hitlag=hitlag,
        state_flags=state_flags,
        instance_id=instance_id,
        last_hit_by=last_hit_by,
        data_root="data",
    )

    assert int(port[1, 0]) == 1
    assert int(iid[1, 0]) == 200
    assert int(port[4, 0]) == 1
    assert int(timer[4, 1]) == 2
    assert int(port[5, 0]) == 1
    assert int(timer[5, 1]) == 1
    assert int(port[6, 0]) == 1
    assert int(timer[6, 1]) == 0
    assert int(port[7, 0]) == 0xFF


def test_combo_push_timer_seed_tracks_combo_count_increment_prefix_invariant() -> None:
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    threshold = int(common["combo_push_count_threshold"])
    frames = int(common["combo_push_timer_frames"])
    assert threshold == 5
    assert frames == 20

    combo_count = np.zeros((10, 4), dtype=np.uint8)
    combo_count[:, 0] = np.array([0, 1, 2, 3, 4, 5, 5, 5, 6, 6], dtype=np.uint8)
    last_attack_landed = np.zeros((10, 4), dtype=np.uint8)
    last_attack_landed[:, 0] = 7
    combo_victim_port = np.full((10, 4), 0xFF, dtype=np.uint8)
    combo_victim_port[:, 0] = 1

    full = derive_combo_push_timer_seed(
        combo_count=combo_count,
        last_attack_landed=last_attack_landed,
        combo_victim_port=combo_victim_port,
        data_root="data",
    )
    assert full[:, 0].tolist() == [0, 0, 0, 0, 0, 20, 19, 18, 20, 19]
    assert not np.any(full[:, 1:])

    for k in range(1, combo_count.shape[0] + 1):
        pref = derive_combo_push_timer_seed(
            combo_count=combo_count[:k].copy(),
            last_attack_landed=last_attack_landed[:k].copy(),
            combo_victim_port=combo_victim_port[:k].copy(),
            data_root="data",
        )
        assert np.array_equal(pref, full[:k])


def test_combo_push_timer_seed_rejects_mixed_attack_or_victim_count_increase() -> None:
    combo_count = np.zeros((8, 4), dtype=np.uint8)
    combo_count[:, 0] = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.uint8)

    mixed_attack = np.zeros((8, 4), dtype=np.uint8)
    mixed_attack[:, 0] = np.array([7, 7, 7, 7, 7, 8, 8, 8], dtype=np.uint8)
    same_victim = np.full((8, 4), 0xFF, dtype=np.uint8)
    same_victim[:, 0] = 1
    mixed_attack_timer = derive_combo_push_timer_seed(
        combo_count=combo_count,
        last_attack_landed=mixed_attack,
        combo_victim_port=same_victim,
        data_root="data",
    )
    assert not np.any(mixed_attack_timer[:, 0])

    same_attack = np.zeros((8, 4), dtype=np.uint8)
    same_attack[:, 0] = 7
    mixed_victim = np.full((8, 4), 0xFF, dtype=np.uint8)
    mixed_victim[:, 0] = np.array([1, 1, 1, 1, 1, 2, 2, 2], dtype=np.uint8)
    mixed_victim_timer = derive_combo_push_timer_seed(
        combo_count=combo_count,
        last_attack_landed=same_attack,
        combo_victim_port=mixed_victim,
        data_root="data",
    )
    assert not np.any(mixed_victim_timer[:, 0])
