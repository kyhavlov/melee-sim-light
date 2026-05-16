from __future__ import annotations

import json

import numpy as np

from tools.slippi.make_dataset_from_slp import _derive_ledge_cooldown


def test_ledge_cooldown_derivation_is_prefix_invariant() -> None:
    """
    Regression test for the "strictly causal" ledge_cooldown derivation.

    The derived cooldown for a prefix of frames must not depend on any future frames.
    """
    common = json.loads(open("data/common/ft_common_data.json").read())

    cooldown_frames = int(common.get("ledge_cooldown_frames", 0))
    assert 0 <= cooldown_frames <= 255

    # Action ids (GALE01): cliff 252..265, fall-like 29..38.
    act_wait = 0x000E
    act_damage_hi_2 = 0x004C
    act_cliff_wait = 0x00FD
    act_cliff_catch = 0x00FC
    act_cliff_climb_quick = 0x00FF
    act_fall = 0x001D

    action_id = np.array(
        [
            *([act_wait] * 3),
            # A cliff segment that ends early (prefix should not "predict" the upcoming fall).
            *([act_cliff_wait] * 6),
            # Cliff -> fall transition sets the cooldown.
            act_fall,
            *([act_fall] * 5),
            *([act_wait] * 2),
            # Another cliff -> fall transition.
            *([act_cliff_catch] * 2),
            act_fall,
            *([act_fall] * 4),
            # Cliff-owned damage entry sets the same source cooldown while old x221D_b7 is live.
            *([act_cliff_climb_quick] * 2),
            act_damage_hi_2,
            *([act_damage_hi_2] * 3),
            # Trailing cliff segment (no fall transition in-range).
            *([act_cliff_wait] * 4),
        ],
        dtype=np.uint16,
    )

    hitlag_u16 = np.zeros(action_id.shape[0], dtype=np.uint16)
    # Pause cooldown decrement once (hitlag[t-1] gate).
    if action_id.shape[0] >= 12:
        hitlag_u16[10] = 1

    full = _derive_ledge_cooldown(action_id_u16=action_id, hitlag_u16=hitlag_u16, common=common)

    for k in (1, 2, 3, 5, 7, 9, 11, 12, 13, 17, 23, int(action_id.size)):
        got = _derive_ledge_cooldown(
            action_id_u16=action_id[:k],
            hitlag_u16=hitlag_u16[:k],
            common=common,
        )
        assert np.array_equal(got, full[:k])

    damage_entry_idx = int(np.flatnonzero(action_id == act_damage_hi_2)[0])
    assert int(full[damage_entry_idx]) == max(0, cooldown_frames - 1)
