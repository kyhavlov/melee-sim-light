from __future__ import annotations

import numpy as np

from tools.slippi.seed_history import derive_grab_owner_port, derive_grab_owner_port_2p


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


def test_grab_owner_derivation_doubles_unique_visible_owner() -> None:
    act_wait = 0x000E
    act_landing_air_lw = 0x004A
    act_catch_pull = 0x00D5
    act_catch_wait = 0x00D8
    act_capture_pulled_lw = 0x00E2
    act_capture_damage_lw = 0x00E4

    # Doubles source-owner reconstruction:
    # - p2 is the attached victim.
    # - p3 is the only visible catch owner.
    # - p0/p1 are active unrelated fighters and must not be selected just because they are alive.
    action_id = np.array(
        [
            [act_wait, act_wait, act_landing_air_lw, act_wait],
            [act_wait, act_wait, act_capture_pulled_lw, act_catch_pull],
            [act_wait, act_wait, act_capture_damage_lw, act_catch_wait],
        ],
        dtype=np.uint16,
    )

    got = derive_grab_owner_port(action_id_u16=action_id, num_players=4)
    expected = np.array(
        [
            [0xFF, 0xFF, 0xFF, 0xFF],
            [0xFF, 0xFF, 3, 0xFF],
            [0xFF, 0xFF, 3, 0xFF],
        ],
        dtype=np.uint8,
    )
    assert np.array_equal(got, expected)


def test_grab_owner_derivation_doubles_ambiguous_pairs_stay_unseeded() -> None:
    act_catch_pull = 0x00D5
    act_capture_pulled_lw = 0x00E2

    action_id = np.array(
        [
            # Two new victim/owner pairs are visible, but current post-frame action ids alone do
            # not encode which owner has which victim_gobj. Keep the hidden seed lane empty rather
            # than inventing a pairing.
            [act_capture_pulled_lw, act_catch_pull, act_capture_pulled_lw, act_catch_pull],
        ],
        dtype=np.uint16,
    )

    got = derive_grab_owner_port(action_id_u16=action_id, num_players=4)
    assert np.array_equal(got, np.array([[0xFF, 0xFF, 0xFF, 0xFF]], dtype=np.uint8))
