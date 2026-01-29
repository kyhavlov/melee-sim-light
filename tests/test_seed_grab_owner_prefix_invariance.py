from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_grab_owner_port_2p


def test_grab_owner_derivation_is_prefix_invariant() -> None:
    """
    Strict prefix invariance: derive(prefix) == derive(full)[:k] for many k.

    This seeds the minimal "who is grabbing whom" identity needed for teacher-forced one-step
    attachment during Thrown*/Capture* victim actions.
    """
    act_wait = 0x000E
    act_thrown_hi = 0x00F1  # ftCo_MS_ThrownHi
    act_capture_damage_lw = 0x00E4  # ftCo_MS_CaptureDamageLw

    # Shape: [n_frames, 2] (slot0, slot1).
    action_id = np.array(
        [
            # No grab.
            [act_wait, act_wait],
            [act_wait, act_wait],
            # Slot1 is captured for a bit (slot0 is owner).
            [act_wait, act_capture_damage_lw],
            [act_wait, act_capture_damage_lw],
            # Slot0 is thrown (slot1 is owner).
            [act_thrown_hi, act_wait],
            [act_thrown_hi, act_wait],
            # Back to neutral.
            [act_wait, act_wait],
        ],
        dtype=np.uint16,
    )

    full = derive_grab_owner_port_2p(action_id_u16_2p=action_id)
    for k in (1, 2, 3, 4, 5, int(action_id.shape[0])):
        got = derive_grab_owner_port_2p(action_id_u16_2p=action_id[:k])
        assert np.array_equal(got, full[:k])


def test_grab_owner_derivation_no_cycles_when_both_victims() -> None:
    act_thrown_hi = 0x00F1  # ftCo_MS_ThrownHi

    action_id = np.array(
        [
            # Impossible/corrupt state: both slots appear as victims.
            [act_thrown_hi, act_thrown_hi],
        ],
        dtype=np.uint16,
    )
    got = derive_grab_owner_port_2p(action_id_u16_2p=action_id)
    assert got.shape == (1, 2)
    assert np.array_equal(got, np.array([[0xFF, 0xFF]], dtype=np.uint8))
