from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_grab_mash_stick_sign_post


def test_grab_mash_sign_derivation_is_prefix_invariant_under_suffix_mutation() -> None:
    # Guard: ftCommon_GrabMash sign latches are strictly causal and depend only on prior/present
    # processed stick samples. Suffix-only mutations must not change earlier derived latch rows.
    stick_x = np.array([0.0, -0.7, -0.2, 0.0, 0.8, 0.1, -0.1, 0.9], dtype=np.float32)
    stick_y = np.array([0.0, 0.0, 0.6, -0.7, -0.2, 0.0, 0.9, -0.9], dtype=np.float32)
    threshold = 0.5

    full_x, full_y = derive_grab_mash_stick_sign_post(
        stick_x_unit=stick_x,
        stick_y_unit=stick_y,
        grab_mash_stick_threshold=threshold,
    )

    assert [int(x) for x in full_x[:6]] == [0, -1, -1, -1, 1, 1]
    assert [int(y) for y in full_y[:6]] == [0, 0, 1, -1, -1, -1]

    cutoff = 6
    mut_x = stick_x.copy()
    mut_y = stick_y.copy()
    mut_x[cutoff:] = np.array([-0.8, -0.9], dtype=np.float32)
    mut_y[cutoff:] = np.array([0.8, 0.9], dtype=np.float32)

    mut_full_x, mut_full_y = derive_grab_mash_stick_sign_post(
        stick_x_unit=mut_x,
        stick_y_unit=mut_y,
        grab_mash_stick_threshold=threshold,
    )

    np.testing.assert_array_equal(full_x[:cutoff], mut_full_x[:cutoff])
    np.testing.assert_array_equal(full_y[:cutoff], mut_full_y[:cutoff])

