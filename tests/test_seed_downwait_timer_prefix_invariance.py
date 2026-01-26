from __future__ import annotations

import json

import numpy as np

from tools.slippi.seed_history import derive_downwait_timer


def test_downwait_timer_derivation_is_prefix_invariant() -> None:
    """
    Regression test for strictly-causal DownWait timer derivation.

    The derived downwait_timer[t] must depend only on action_id[0..t] (no lookahead).
    """
    common = json.loads(open("data/common/ft_common_data.json").read())

    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_down_bound_u = 0x00B7
    act_down_wait_u = 0x00B8
    act_down_stand_u = 0x00BA
    act_down_bound_d = 0x00BF
    act_down_wait_d = 0x00C0
    act_down_stand_d = 0x00C2

    action_id = np.array(
        [
            *([act_wait] * 5),
            *([act_down_bound_u] * 3),
            *([act_down_wait_u] * 10),
            *([act_down_stand_u] * 4),
            *([act_wait] * 3),
            *([act_down_bound_d] * 2),
            *([act_down_wait_d] * 7),
            *([act_down_stand_d] * 5),
            *([act_wait] * 6),
        ],
        dtype=np.uint16,
    )

    full = derive_downwait_timer(
        action_id_u16=action_id,
        down_wait_frames=int(common["down_wait_frames"]),
        act_down_wait_u=act_down_wait_u,
        act_down_wait_d=act_down_wait_d,
    )

    for k in (1, 2, 3, 5, 6, 9, 13, 17, 23, 31, int(action_id.size)):
        got = derive_downwait_timer(
            action_id_u16=action_id[:k],
            down_wait_frames=int(common["down_wait_frames"]),
            act_down_wait_u=act_down_wait_u,
            act_down_wait_d=act_down_wait_d,
        )
        assert np.array_equal(got, full[:k])

