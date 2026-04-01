from __future__ import annotations

import numpy as np

from tools.slippi.make_dataset_from_slp import (
    _derive_source_clear_processhit_damage_pending_phase_seed_lane,
)


def test_source_clear_processhit_damage_pending_phase_prefix_invariance_suffix_mutation() -> None:
    # Guard: the foundational ProcessHit damage-pending seed lane must remain strictly causal even
    # while its producer is behavior-neutral.
    n = 8
    action_id = np.array([24, 24, 24, 24, 20, 20, 20, 24], dtype=np.uint16)
    action_frame = np.array([0, 0, 1, 2, 1, 2, 3, 0], dtype=np.int16)
    on_ground = np.ones(n, dtype=np.uint8)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.zeros(n, dtype=np.uint16)
    combo_count = np.array([0, 0, 0, 0, 3, 3, 3, 3], dtype=np.uint8)
    last_attack_landed = np.array([21, 21, 21, 21, 55, 55, 55, 55], dtype=np.uint8)
    source_timer = np.array([40, 39, 38, 37, 49, 48, 47, 46], dtype=np.uint8)
    owner_phase = np.ones(n, dtype=np.uint8)
    colanim = np.array([1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    last_hit_by = np.array([1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)

    base = _derive_source_clear_processhit_damage_pending_phase_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        on_ground_u8=on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        combo_count_u8=combo_count,
        last_attack_landed_u8=last_attack_landed,
        source_clear_timer_x18c8_u8=source_timer,
        source_clear_owner_set_phase_u8=owner_phase,
        colanim_hit_status_x198c_u8=colanim,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
    )

    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_on_ground = on_ground.copy()
    mut_combo_count = combo_count.copy()
    mut_last_attack_landed = last_attack_landed.copy()
    mut_source_timer = source_timer.copy()
    mut_colanim = colanim.copy()
    mut_last_hit_by = last_hit_by.copy()

    mut_action_id[cutoff:] = np.array([14, 14, 14], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([0, 1, 2], dtype=np.int16)
    mut_on_ground[cutoff:] = np.array([1, 1, 1], dtype=np.uint8)
    mut_combo_count[cutoff:] = np.array([1, 1, 1], dtype=np.uint8)
    mut_last_attack_landed[cutoff:] = np.array([13, 13, 13], dtype=np.uint8)
    mut_source_timer[cutoff:] = np.array([9, 8, 7], dtype=np.uint8)
    mut_colanim[cutoff:] = np.array([0, 0, 0], dtype=np.uint8)
    mut_last_hit_by[cutoff:] = np.array([0, 0, 0], dtype=np.uint8)

    mutated = _derive_source_clear_processhit_damage_pending_phase_seed_lane(
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        on_ground_u8=mut_on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        combo_count_u8=mut_combo_count,
        last_attack_landed_u8=mut_last_attack_landed,
        source_clear_timer_x18c8_u8=mut_source_timer,
        source_clear_owner_set_phase_u8=owner_phase,
        colanim_hit_status_x198c_u8=mut_colanim,
        state_flags_u8=state_flags,
        last_hit_by_u8=mut_last_hit_by,
    )

    assert int(base[3]) == 0
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])
