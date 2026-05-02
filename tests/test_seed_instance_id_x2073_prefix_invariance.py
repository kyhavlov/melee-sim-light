from __future__ import annotations

import numpy as np

from tools.slippi.action_state_tables import load_action_state_tables
from tools.slippi.seed_history import derive_instance_id_x2073


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Fox/Falco down special (SpecialLw / Shine) start (GALE01): 360.
# refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c (commented numeric ids)
ACT_FX_SPECIAL_LW_START = 0x0168

CHAR_FOX = 1


def test_seed_instance_id_x2073_derivation_is_prefix_invariant() -> None:
    """
    Regression test for the "strictly causal" fp+0x2073 (instance_id compare byte) derivation.

    The derived x2073 for a prefix of frames must not depend on any future frames.
    """
    # Build a sequence with multiple action transitions.
    action_id = np.array(
        [
            *([ACT_WAIT] * 3),
            ACT_FX_SPECIAL_LW_START,
            *([ACT_FX_SPECIAL_LW_START] * 4),
            ACT_WAIT,
            *([ACT_WAIT] * 2),
            ACT_FX_SPECIAL_LW_START,
            *([ACT_FX_SPECIAL_LW_START] * 2),
        ],
        dtype=np.uint16,
    )

    # Use a monotonic action_frame ramp (we only use action_id transitions here).
    action_frame = np.arange(action_id.size, dtype=np.int16)
    char_id = np.full(action_id.shape[0], np.uint8(CHAR_FOX), dtype=np.uint8)

    full = derive_instance_id_x2073(
        char_id_u8=char_id,
        action_id_u16=action_id,
        action_frame_i16=action_frame,
    )

    for k in (1, 2, 3, 4, 7, 10, int(action_id.size)):
        got = derive_instance_id_x2073(
            char_id_u8=char_id[:k],
            action_id_u16=action_id[:k],
            action_frame_i16=action_frame[:k],
        )
        assert np.array_equal(got, full[:k])

    # Intentional generated-data dependency: this guard verifies the native derivation reads the
    # same MSLACID1 table that normal preprocessing uses. tests/conftest.py ensures it exists.
    table = load_action_state_tables("data")[CHAR_FOX]
    # Sanity: on motion-state entry frames, x2073 becomes the generated table's low byte.
    for i in range(int(action_id.size)):
        if i == 0 or int(action_id[i]) != int(action_id[i - 1]):
            assert int(full[i]) == int(table.x4_flags_low[int(action_id[i])])
