from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_seed_prev_action_post


def test_seed_prev_action_derivation_is_prefix_invariant_under_suffix_mutation() -> None:
    # Guard: seed_prev_action_* is a strictly causal replay-history lane.
    # Suffix-only mutations in post-frame action history must not perturb earlier derived rows.
    action_id = np.array([14, 20, 352, 43, 43, 24, 24, 14], dtype=np.uint16)
    action_frame = np.array([3, 7, 19, 0, 1, 0, 1, 5], dtype=np.int16)

    full_prev_id, full_prev_frame = derive_seed_prev_action_post(
        post_action_id_u16=action_id,
        post_action_frame_i16=action_frame,
    )

    assert [int(x) for x in full_prev_id[:5]] == [14, 14, 20, 352, 43]
    assert [int(x) for x in full_prev_frame[:5]] == [3, 3, 7, 19, 0]

    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_action_id[cutoff:] = np.array([181, 360, 43], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([0, -1, 4], dtype=np.int16)

    mut_prev_id, mut_prev_frame = derive_seed_prev_action_post(
        post_action_id_u16=mut_action_id,
        post_action_frame_i16=mut_action_frame,
    )

    np.testing.assert_array_equal(full_prev_id[:cutoff], mut_prev_id[:cutoff])
    np.testing.assert_array_equal(full_prev_frame[:cutoff], mut_prev_frame[:cutoff])

