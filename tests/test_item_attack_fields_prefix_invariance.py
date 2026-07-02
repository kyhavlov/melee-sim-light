from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import ITEM_DTYPE
from tools.slippi.validation_buffer_items import _derive_item_attack_fields


def test_item_attack_fields_prefix_invariance_with_owner_change() -> None:
    # Strict prefix invariance: derive(prefix) == derive(full)[:k] for many k.
    #
    # Synthetic scenario:
    # - A single item persists across frames with constant (spawn_id,type)
    # - At t=3, the item changes owner and also changes instance_id (as happens on reflect)
    n_frames = 7
    num_players = 2

    items_full = np.zeros((n_frames, 15), dtype=ITEM_DTYPE)
    # Persistent item identity (spawn_id,type)
    items_full[:, 0]["exists"] = np.uint8(1)
    items_full[:, 0]["spawn_id"] = np.uint32(123)
    items_full[:, 0]["type"] = np.uint16(999)

    # Owner/instance identity changes at t=3.
    items_full[:3, 0]["owner"] = np.int8(0)
    items_full[3:, 0]["owner"] = np.int8(1)
    items_full[:3, 0]["instance_id"] = np.uint16(100)
    items_full[3:, 0]["instance_id"] = np.uint16(200)

    # Fighter-side identity is allowed to vary per frame; item should snapshot on first sight and
    # remain spawn-latched across owner/instance_id churn (e.g. reflect).
    fighter_attack_id = np.zeros((n_frames, num_players), dtype=np.uint16)
    fighter_attack_instance = np.zeros((n_frames, num_players), dtype=np.uint16)
    for t in range(n_frames):
        fighter_attack_id[t, 0] = np.uint16(1000 + t)
        fighter_attack_id[t, 1] = np.uint16(2000 + t)
        fighter_attack_instance[t, 0] = np.uint16(10 + t)
        fighter_attack_instance[t, 1] = np.uint16(20 + t)

    full = items_full.copy()
    _derive_item_attack_fields(
        full,
        fighter_attack_id=fighter_attack_id,
        fighter_attack_instance=fighter_attack_instance,
        num_players=num_players,
    )

    # Spawn-latched: derived from owner=0 on first sight (frame 0) and does not change on
    # subsequent owner/instance_id churn (e.g. reflect).
    assert np.all(full[:, 0]["attack_id"] == np.uint16(fighter_attack_id[0, 0]))
    assert np.all(full[:, 0]["attack_instance"] == np.uint16(fighter_attack_instance[0, 0]))

    ks = [1, 2, 3, 4, 5, n_frames - 1, n_frames]
    ks = [k for k in ks if 1 <= k <= n_frames]
    for k in ks:
        pref = items_full[:k].copy()
        _derive_item_attack_fields(
            pref,
            fighter_attack_id=fighter_attack_id[:k],
            fighter_attack_instance=fighter_attack_instance[:k],
            num_players=num_players,
        )
        assert np.array_equal(pref["attack_id"], full[:k]["attack_id"])
        assert np.array_equal(pref["attack_instance"], full[:k]["attack_instance"])
