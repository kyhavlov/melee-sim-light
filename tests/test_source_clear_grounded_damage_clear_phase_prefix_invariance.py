from __future__ import annotations

import numpy as np

from tools.slippi.validation_buffer_seed import (
    _derive_source_clear_grounded_damage_clear_phase_seed_lane,
)


def test_source_clear_grounded_damage_clear_phase_prefix_invariance_suffix_mutation() -> None:
    # Guard: derivation must be strictly causal (t / t-1->t only).
    # Mutating suffix rows must not change derived prefix rows.
    n = 9
    action_id = np.array([0x0010, 0x000E, 0x0014, 0x0015, 0x0015, 0x0014, 0x0014, 0x0014, 0x0014], dtype=np.uint16)
    action_frame = np.array([3, 0, 1, 0, 1, 2, 3, 4, 5], dtype=np.int16)
    on_ground = np.array([0, 1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.zeros(n, dtype=np.uint16)
    combo_count = np.array([1, 0, 0, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    source_timer = np.array([13, 12, 11, 10, 9, 8, 7, 6, 5], dtype=np.uint8)
    source_owner_phase = np.array([0, 1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    last_hit_by = np.array([6, 1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)

    base = _derive_source_clear_grounded_damage_clear_phase_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        on_ground_u8=on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        combo_count_u8=combo_count,
        source_clear_timer_x18c8_u8=source_timer,
        source_clear_owner_set_phase_u8=source_owner_phase,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
    )

    cutoff = 6
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_on_ground = on_ground.copy()
    mut_hitlag = hitlag.copy()
    mut_hitstun = hitstun.copy()
    mut_combo_count = combo_count.copy()
    mut_source_timer = source_timer.copy()
    mut_source_owner_phase = source_owner_phase.copy()
    mut_state_flags = state_flags.copy()
    mut_last_hit_by = last_hit_by.copy()

    mut_action_id[cutoff:] = np.array([0x00B2, 0x00B2, 0x000E], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([0, 1, 2], dtype=np.int16)
    mut_on_ground[cutoff:] = np.array([1, 1, 1], dtype=np.uint8)
    mut_hitlag[cutoff:] = np.array([0, 1, 0], dtype=np.uint16)
    mut_hitstun[cutoff:] = np.array([0, 0, 1], dtype=np.uint16)
    mut_combo_count[cutoff:] = np.array([1, 0, 1], dtype=np.uint8)
    mut_source_timer[cutoff:] = np.array([4, 3, 2], dtype=np.uint8)
    mut_source_owner_phase[cutoff:] = np.array([1, 1, 1], dtype=np.uint8)
    mut_state_flags[cutoff:, 4] = np.array([0, 0x10, 0], dtype=np.uint8)
    mut_last_hit_by[cutoff:] = np.array([1, 1, 6], dtype=np.uint8)

    mutated = _derive_source_clear_grounded_damage_clear_phase_seed_lane(
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        on_ground_u8=mut_on_ground,
        hitlag_u16=mut_hitlag,
        hitstun_u16=mut_hitstun,
        combo_count_u8=mut_combo_count,
        source_clear_timer_x18c8_u8=mut_source_timer,
        source_clear_owner_set_phase_u8=mut_source_owner_phase,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
    )

    assert int(base[1]) == 1
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])
