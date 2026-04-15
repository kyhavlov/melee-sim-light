from __future__ import annotations

import numpy as np

from tools.slippi.make_dataset_from_slp import (
    _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane,
)


def test_fighter_8006cda4_pre_gate_consume_count_prefix_invariance_suffix_mutation() -> None:
    n = 8
    action_id = np.array([24, 24, 67, 67, 239, 239, 90, 90], dtype=np.uint16)
    action_frame = np.array([0, 1, 6, 7, 9, 10, 55, 56], dtype=np.int16)
    on_ground = np.array([1, 1, 0, 0, 1, 1, 0, 0], dtype=np.uint8)
    hitlag = np.array([0, 0, 0, 0, 0, 1, 0, 0], dtype=np.uint16)
    hitstun = np.array([0, 0, 0, 0, 0, 0, 4, 8], dtype=np.uint16)
    last_hit_by = np.array([6, 6, 6, 6, 6, 6, 0, 0], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    state_flags[5, 1] = 0x30
    all_action_id = np.zeros((n, 2), dtype=np.uint16)
    all_action_frame = np.zeros((n, 2), dtype=np.int16)
    all_action_id[:, 0] = np.array([24, 24, 67, 67, 24, 24, 67, 67], dtype=np.uint16)
    all_action_frame[:, 0] = np.array([0, 1, 6, 7, 0, 0, 5, 6], dtype=np.int16)
    all_action_id[:, 1] = action_id
    all_action_frame[:, 1] = action_frame

    base = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        on_ground_u8=on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        victim_port=1,
        num_players=2,
    )

    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_on_ground = on_ground.copy()
    mut_hitlag = hitlag.copy()
    mut_hitstun = hitstun.copy()
    mut_last_hit_by = last_hit_by.copy()
    mut_state_flags = state_flags.copy()
    mut_all_action_id = all_action_id.copy()
    mut_all_action_frame = all_action_frame.copy()

    mut_action_id[cutoff:] = np.array([90, 90, 90], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([55, 56, 57], dtype=np.int16)
    mut_on_ground[cutoff:] = np.array([0, 0, 0], dtype=np.uint8)
    mut_hitlag[cutoff:] = np.array([0, 0, 0], dtype=np.uint16)
    mut_hitstun[cutoff:] = np.array([4, 3, 2], dtype=np.uint16)
    mut_last_hit_by[cutoff:] = np.array([0, 0, 0], dtype=np.uint8)
    mut_state_flags[cutoff:, :] = 0
    mut_all_action_id[cutoff:, 0] = np.array([67, 67, 67], dtype=np.uint16)
    mut_all_action_frame[cutoff:, 0] = np.array([6, 7, 8], dtype=np.int16)

    mutated = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        on_ground_u8=mut_on_ground,
        hitlag_u16=mut_hitlag,
        hitstun_u16=mut_hitstun,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
        all_action_id_u16=mut_all_action_id,
        all_action_frame_i16=mut_all_action_frame,
        victim_port=1,
        num_players=2,
    )

    assert int(base[2]) == 1
    assert int(base[5]) == 2
    assert int(base[7]) == 2
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])
