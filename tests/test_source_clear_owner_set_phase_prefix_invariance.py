from __future__ import annotations

import numpy as np

from tools.slippi.make_dataset_from_slp import (
    _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes,
)


def test_source_clear_owner_set_phase_prefix_invariance_suffix_mutation() -> None:
    # Guard: source-owner set phase derivation must be strictly causal (t / t-1->t only).
    # Mutating suffix rows must not change derived prefix rows.
    n = 10
    action_id = np.array([14, 25, 25, 25, 25, 25, 25, 25, 25, 25], dtype=np.uint16)
    char_id = np.array([1] * n, dtype=np.uint8)
    on_ground = np.array([1] * n, dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    # Source-owner acquire edge at row 1 (6 -> 1), then stable ownership.
    last_hit_by = np.array([6, 1, 1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)

    x9_b1_tbl = np.zeros(64, dtype=np.uint8)
    # ftCo_MS_JumpF can seed x18C8 on grounded motion-state entry under decomp x9_b1 gating.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    x9_b1_tbl[25] = np.uint8(1)

    timer_base, phase_base = _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(
        action_id_u16=action_id,
        char_id_u8=char_id,
        on_ground_u8=on_ground,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
        x9_b1_by_char={1: x9_b1_tbl},
        source_clear_init_frames=60,
    )

    cutoff = 6
    mut_action_id = action_id.copy()
    mut_char_id = char_id.copy()
    mut_on_ground = on_ground.copy()
    mut_state_flags = state_flags.copy()
    mut_last_hit_by = last_hit_by.copy()

    # Mutate suffix only.
    mut_action_id[cutoff:] = np.array([0x0012, 0x0019, 0x001A, 0x000E], dtype=np.uint16)
    mut_char_id[cutoff:] = np.array([1, 1, 22, 22], dtype=np.uint8)
    mut_on_ground[cutoff:] = np.array([1, 1, 0, 1], dtype=np.uint8)
    mut_state_flags[cutoff:, 4] = np.array([0x10, 0x00, 0x10, 0x00], dtype=np.uint8)
    mut_last_hit_by[cutoff:] = np.array([1, 1, 6, 0], dtype=np.uint8)

    timer_mut, phase_mut = _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(
        action_id_u16=mut_action_id,
        char_id_u8=mut_char_id,
        on_ground_u8=mut_on_ground,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
        x9_b1_by_char={1: x9_b1_tbl, 22: x9_b1_tbl},
        source_clear_init_frames=60,
    )

    assert int(timer_base[1]) > 0
    assert int(phase_base[1]) == 1
    np.testing.assert_array_equal(timer_base[:cutoff], timer_mut[:cutoff])
    np.testing.assert_array_equal(phase_base[:cutoff], phase_mut[:cutoff])
