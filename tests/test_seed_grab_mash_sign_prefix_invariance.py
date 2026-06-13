from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_grab_mash_stick_sign_post

ACT_CAPTURE_PULLED_LW = 0x00E2
ACT_CAPTURE_WAIT_LW = 0x00E3


def _derive(stick_x, stick_y, action_id, action_frame, owner, threshold):
    return derive_grab_mash_stick_sign_post(
        stick_x_unit=stick_x,
        stick_y_unit=stick_y,
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        grab_owner_port_u8=owner,
        grab_mash_stick_threshold=threshold,
    )


def test_grab_mash_sign_derivation_is_prefix_invariant_under_suffix_mutation() -> None:
    # Guard: ftCommon_GrabMash sign latches are strictly causal and depend only on prior/present
    # rows. Suffix-only mutations must not change earlier derived latch rows.
    #
    # The latches are GrabMash-scheduled (v12 Dolphin probe windows GAT 2482/5677, AGNG 3208,
    # QGD 8257, CDO 12724, PRH 7602): cleared at capture attach, updated only on frames whose
    # CaptureWait/CaptureDamage callback runs GrabMash, reading fp-visible sticks that lag the
    # serialized rows by one.
    stick_x = np.array([0.0, -0.7, -0.2, 0.0, 0.8, 0.1, -0.1, 0.9], dtype=np.float32)
    stick_y = np.array([0.0, 0.0, 0.6, -0.7, -0.2, 0.0, 0.9, -0.9], dtype=np.float32)
    threshold = 0.5
    n = stick_x.shape[0]
    # Attach at row 1 (CapturePulledLw), steady CaptureWaitLw from row 2 onward: GrabMash runs
    # on every frame whose start-state is wait, i.e. steps landing on rows 3..7.
    action_id = np.full(n, ACT_CAPTURE_WAIT_LW, dtype=np.uint16)
    action_id[0] = 0x000E  # not attached yet
    action_id[1] = ACT_CAPTURE_PULLED_LW
    action_frame = np.array([0, 0, 0, 1, 2, 3, 4, 5], dtype=np.int16)
    owner = np.full(n, 1, dtype=np.uint8)
    owner[0] = 0xFF

    full_x, full_y = _derive(stick_x, stick_y, action_id, action_frame, owner, threshold)

    # Row 1: attach clear. Rows 3+: latch update from the lagged stick (row i-1):
    #   row 3 reads (-0.2, 0.6) -> y latches +1; row 4 reads (0.0, -0.7) -> y latches -1;
    #   row 5 reads (0.8, -0.2) -> x latches +1; rows 6/7 read sub-threshold x and
    #   (-0.1, 0.9)/(0.9, ...) respectively.
    assert [int(x) for x in full_x] == [0, 0, 0, 0, 0, 1, 1, 1]
    assert [int(y) for y in full_y] == [0, 0, 0, 1, -1, -1, -1, 1]

    cutoff = 6
    mut_x = stick_x.copy()
    mut_y = stick_y.copy()
    mut_x[cutoff:] = np.array([-0.8, -0.9], dtype=np.float32)
    mut_y[cutoff:] = np.array([0.8, 0.9], dtype=np.float32)

    mut_full_x, mut_full_y = _derive(mut_x, mut_y, action_id, action_frame, owner, threshold)

    np.testing.assert_array_equal(full_x[:cutoff], mut_full_x[:cutoff])
    np.testing.assert_array_equal(full_y[:cutoff], mut_full_y[:cutoff])


def test_grab_mash_sign_latches_freeze_outside_grab_states() -> None:
    # The latch lanes are frozen outside CaptureWait/CaptureDamage frames and cleared by
    # ftCommon_InitGrab at a fresh attach -- pre-grab stick history must not pre-latch into
    # the first wait mash check.
    n = 6
    stick_x = np.array([0.9, 0.9, 0.0, 0.0, 0.0, 0.0], dtype=np.float32)
    stick_y = np.zeros(n, dtype=np.float32)
    action_id = np.array(
        [0x000E, 0x000E, ACT_CAPTURE_PULLED_LW, ACT_CAPTURE_WAIT_LW, ACT_CAPTURE_WAIT_LW,
         ACT_CAPTURE_WAIT_LW],
        dtype=np.uint16,
    )
    action_frame = np.array([0, 1, 0, 0, 1, 2], dtype=np.int16)
    owner = np.array([0xFF, 0xFF, 1, 1, 1, 1], dtype=np.uint8)

    full_x, full_y = _derive(stick_x, stick_y, action_id, action_frame, owner, 0.5)

    # The held 0.9 x-stick before the grab never latches (GrabMash not running), and the
    # attach clear keeps the lanes at 0 through the wait entry.
    assert [int(x) for x in full_x] == [0, 0, 0, 0, 0, 0]
    assert [int(y) for y in full_y] == [0, 0, 0, 0, 0, 0]
