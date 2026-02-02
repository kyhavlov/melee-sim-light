from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.slippi.seed_history import derive_run_x0


def test_run_x0_derivation_is_prefix_invariant() -> None:
    """
    Regression test for strictly-causal Run IASA lockout derivation.

    The derived run_x0[t] must depend only on history up to t (no lookahead).
    """
    common = json.loads(Path("data/common/ft_common_data.json").read_text())

    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_turn_run = 0x0013
    act_run = 0x0015
    act_run_direct = 0x0016

    action_id = np.array(
        [
            *([act_wait] * 5),
            *([act_turn_run] * 8),
            *([act_run] * 12),
            *([act_wait] * 3),
            *([act_turn_run] * 4),
            *([act_run_direct] * 6),
            *([act_wait] * 10),
        ],
        dtype=np.uint16,
    )
    hitlag = np.zeros(action_id.shape[0], dtype=np.uint16)

    full = derive_run_x0(
        action_id=action_id,
        hitlag_u16=hitlag,
        run_x0_init_x430=float(common["run_x0_init_x430"]),
        act_run=act_run,
        act_run_direct=act_run_direct,
        act_turn_run=act_turn_run,
    )

    for k in (1, 2, 3, 5, 6, 9, 13, 17, 23, 31, int(action_id.size)):
        got = derive_run_x0(
            action_id=action_id[:k],
            hitlag_u16=hitlag[:k],
            run_x0_init_x430=float(common["run_x0_init_x430"]),
            act_run=act_run,
            act_run_direct=act_run_direct,
            act_turn_run=act_turn_run,
        )
        assert np.array_equal(got, full[:k])
