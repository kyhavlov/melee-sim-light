from __future__ import annotations

import numpy as np

from tools.slippi.make_dataset_from_slp import _derive_source_clear_terminal_phase_seed_lane


def test_source_clear_terminal_phase_prefix_invariance_suffix_mutation() -> None:
    # Guard: seed lane derivation must be strictly causal (t / t-1->t only).
    # Mutating suffix rows must not change derived prefix rows.
    n = 8
    # Use a decomp-defined downed recovery action id that is in the modeled allowlist.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_DownFowardD = 0x00C4)
    action_id = np.array([0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4], dtype=np.uint16)
    action_frame = np.array([5, 6, 7, 8, 9, 10, 11, 12], dtype=np.int16)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.zeros(n, dtype=np.uint16)
    combo_count = np.ones(n, dtype=np.uint8)
    last_attack_landed = np.full(n, 15, dtype=np.uint8)
    source_clear_timer = np.array([3, 2, 1, 0, 0, 2, 1, 0], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    last_hit_by = np.array([1, 1, 1, 6, 6, 0, 0, 6], dtype=np.uint8)

    base = _derive_source_clear_terminal_phase_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        combo_count_u8=combo_count,
        last_attack_landed_u8=last_attack_landed,
        source_clear_timer_x18c8_u8=source_clear_timer,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
    )

    # Mutate only suffix rows [cutoff:].
    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_hitlag = hitlag.copy()
    mut_hitstun = hitstun.copy()
    mut_combo_count = combo_count.copy()
    mut_last_attack_landed = last_attack_landed.copy()
    mut_source_clear_timer = source_clear_timer.copy()
    mut_state_flags = state_flags.copy()
    mut_last_hit_by = last_hit_by.copy()

    mut_action_id[cutoff:] = np.array([0x0014, 0x0014, 0x0014], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([1, 2, 3], dtype=np.int16)
    mut_hitlag[cutoff:] = np.array([0, 1, 0], dtype=np.uint16)
    mut_hitstun[cutoff:] = np.array([0, 2, 0], dtype=np.uint16)
    mut_combo_count[cutoff:] = np.array([1, 0, 1], dtype=np.uint8)
    mut_last_attack_landed[cutoff:] = np.array([12, 0, 12], dtype=np.uint8)
    mut_source_clear_timer[cutoff:] = np.array([2, 1, 0], dtype=np.uint8)
    mut_state_flags[cutoff:, 1] = np.array([0, 8, 0], dtype=np.uint8)
    mut_last_hit_by[cutoff:] = np.array([0, 0, 6], dtype=np.uint8)

    mutated = _derive_source_clear_terminal_phase_seed_lane(
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        hitlag_u16=mut_hitlag,
        hitstun_u16=mut_hitstun,
        combo_count_u8=mut_combo_count,
        last_attack_landed_u8=mut_last_attack_landed,
        source_clear_timer_x18c8_u8=mut_source_clear_timer,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
    )

    assert int(base[2]) == 1, "expected a modeled terminal-phase row in the prefix"
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])
