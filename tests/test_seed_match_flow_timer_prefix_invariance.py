from __future__ import annotations

import json

import numpy as np

from tools.slippi.make_dataset_from_slp import _derive_match_flow_timer


def test_match_flow_timer_derivation_is_prefix_invariant() -> None:
    """
    Regression test for the "strictly causal" match_flow_timer derivation.

    The derived countdown for a prefix of frames must not depend on any future frames.
    """
    common = json.loads(open("data/common/ft_common_data.json").read())

    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_dead_down = 0x0000
    act_dead_up_star = 0x0004
    act_dead_up_fall = 0x0006
    act_dead_up_fall_hitcamera = 0x0007
    act_rebirth = 0x000C
    act_rebirth_wait = 0x000D
    act_entry = 0x0142
    act_entry_start = 0x0143
    act_entry_end = 0x0144

    # Port 1 (0-indexed): Entry delay is port-dependent.
    port0 = 0

    # Mix match-flow and non-match-flow states, and include runs that end early in the replay
    # (RebirthWait) to ensure we don't accidentally encode lookahead "remaining until action ends".
    action_id = np.array(
        [
            # Entry delay (per-port).
            *([act_entry] * 5),
            # EntryStart/EntryEnd (timer-driven).
            *([act_entry_start] * 7),
            *([act_entry_end] * 3),
            # Back to normal gameplay.
            *([act_wait] * 4),
            # Death states.
            *([act_dead_down] * 6),
            *([act_dead_up_star] * 5),
            *([act_dead_up_fall] * 4),
            *([act_dead_up_fall_hitcamera] * 5),
            # Respawn states.
            *([act_rebirth] * 8),
            # RebirthWait often exits early (input-based), so we intentionally stop the run early.
            *([act_rebirth_wait] * 3),
            *([act_wait] * 6),
        ],
        dtype=np.uint16,
    )

    full = _derive_match_flow_timer(action_id_u16=action_id, port0=port0, common=common)

    for k in (1, 2, 3, 5, 6, 9, 13, 17, 23, 31, 47, int(action_id.size)):
        got = _derive_match_flow_timer(action_id_u16=action_id[:k], port0=port0, common=common)
        assert np.array_equal(got, full[:k])
