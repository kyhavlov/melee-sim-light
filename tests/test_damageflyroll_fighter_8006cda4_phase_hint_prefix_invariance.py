from __future__ import annotations

import numpy as np

from tools.slippi.make_dataset_from_slp import (
    _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane,
)


def test_fighter_8006cda4_pre_gate_consume_count_prefix_invariance_suffix_mutation() -> None:
    n = 14
    action_id = np.array([24, 24, 67, 67, 239, 239, 90, 90, 74, 57, 363, 74, 74, 363], dtype=np.uint16)
    action_frame = np.array([0, 1, 6, 7, 9, 10, 55, 56, 3, 8, 10, 0, 16, 3], dtype=np.int16)
    on_ground = np.array([1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    hitlag = np.array([0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    hitstun = np.array([0, 0, 0, 0, 0, 0, 4, 8, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    ref_action_id = action_id.copy()
    frame_pre_random_seed = np.full(n, 12345, dtype=np.uint32)
    last_hit_by = np.array([6, 6, 6, 6, 6, 6, 0, 0, 6, 6, 6, 6, 6, 6], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    state_flags[5, 1] = 0x30
    source_port0 = np.zeros((n, 2), dtype=np.uint8)
    source_port0[:, 0] = 0
    source_port0[:, 1] = 1
    all_action_id = np.zeros((n, 2), dtype=np.uint16)
    all_action_frame = np.zeros((n, 2), dtype=np.int16)
    all_action_id[:, 0] = np.array([24, 24, 67, 67, 24, 24, 67, 67, 24, 24, 24, 24, 24, 24], dtype=np.uint16)
    all_action_frame[:, 0] = np.array([0, 1, 6, 7, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0], dtype=np.int16)
    all_action_id[:, 1] = action_id
    all_action_frame[:, 1] = action_frame

    base = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        ref_action_id_u16=ref_action_id,
        on_ground_u8=on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        frame_pre_random_seed_u32=frame_pre_random_seed,
        damagefly_roll_prob=0.3,
        victim_port=1,
        num_players=2,
    )

    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_on_ground = on_ground.copy()
    mut_hitlag = hitlag.copy()
    mut_hitstun = hitstun.copy()
    mut_ref_action_id = ref_action_id.copy()
    mut_frame_pre_random_seed = frame_pre_random_seed.copy()
    mut_last_hit_by = last_hit_by.copy()
    mut_state_flags = state_flags.copy()
    mut_source_port0 = source_port0.copy()
    mut_all_action_id = all_action_id.copy()
    mut_all_action_frame = all_action_frame.copy()

    mut_action_id[cutoff:] = np.array([90, 90, 90, 90, 90, 90, 90, 90, 90], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([55, 56, 57, 58, 59, 60, 61, 62, 63], dtype=np.int16)
    mut_on_ground[cutoff:] = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint8)
    mut_hitlag[cutoff:] = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint16)
    mut_hitstun[cutoff:] = np.array([4, 3, 2, 1, 1, 1, 1, 1, 1], dtype=np.uint16)
    mut_ref_action_id[cutoff:] = np.array([91, 91, 88, 88, 88, 88, 88, 88, 88], dtype=np.uint16)
    mut_frame_pre_random_seed[cutoff:] = np.arange(9000, 9009, dtype=np.uint32)
    mut_last_hit_by[cutoff:] = np.array([0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.uint8)
    mut_state_flags[cutoff:, :] = 0
    mut_all_action_id[cutoff:, 0] = np.array([67, 67, 67, 67, 67, 67, 67, 67, 67], dtype=np.uint16)
    mut_all_action_frame[cutoff:, 0] = np.array([6, 7, 8, 9, 10, 11, 12, 13, 14], dtype=np.int16)

    mutated = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        ref_action_id_u16=mut_ref_action_id,
        on_ground_u8=mut_on_ground,
        hitlag_u16=mut_hitlag,
        hitstun_u16=mut_hitstun,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
        all_source_port0_u8=mut_source_port0,
        all_action_id_u16=mut_all_action_id,
        all_action_frame_i16=mut_all_action_frame,
        frame_pre_random_seed_u32=mut_frame_pre_random_seed,
        damagefly_roll_prob=0.3,
        victim_port=1,
        num_players=2,
    )

    assert int(base[2]) == 0
    assert int(base[5]) == 2
    assert int(base[7]) == 2
    assert int(base[8]) == 1
    assert int(base[9]) == 1
    assert int(base[10]) == 1
    assert int(base[11]) == 2
    assert int(base[12]) == 2
    assert int(base[13]) == 0
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])


def test_fighter_8006cda4_pre_gate_consume_count_maps_raw_source_port_to_local_slot() -> None:
    n = 1
    out = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=np.array([90], dtype=np.uint16),
        action_frame_i16=np.array([12], dtype=np.int16),
        ref_action_id_u16=np.array([88], dtype=np.uint16),
        on_ground_u8=np.array([0], dtype=np.uint8),
        hitlag_u16=np.array([0], dtype=np.uint16),
        hitstun_u16=np.array([5], dtype=np.uint16),
        state_flags_u8=np.zeros((n, 5), dtype=np.uint8),
        last_hit_by_u8=np.array([3], dtype=np.uint8),
        all_source_port0_u8=np.array([[1, 3]], dtype=np.uint8),
        all_action_id_u16=np.array([[90, 67]], dtype=np.uint16),
        all_action_frame_i16=np.array([[12, 8]], dtype=np.int16),
        frame_pre_random_seed_u32=np.array([12345], dtype=np.uint32),
        damagefly_roll_prob=0.3,
        victim_port=0,
        num_players=2,
    )

    assert int(out[0]) == 2


def test_fighter_8006cda4_pre_gate_consume_count_accounts_for_same_frame_global_rng_order() -> None:
    # Same-frame reciprocal aerial contacts share the global HSD RNG stream. The later
    # attacker-ordered victim must derive its explicit Fighter_8006CDA4 stream phase after the
    # earlier victim's DamageFlyRoll gate sample.
    n = 1
    source_port0 = np.array([[0, 1]], dtype=np.uint8)
    all_action_id = np.array([[65, 67]], dtype=np.uint16)
    all_action_frame = np.array([[10, 3]], dtype=np.int16)
    all_ref_action_id = np.array([[91, 91]], dtype=np.uint16)
    all_on_ground = np.array([[0, 0]], dtype=np.uint8)
    all_hitlag = np.array([[0, 0]], dtype=np.uint16)
    all_hitstun = np.array([[0, 0]], dtype=np.uint16)
    all_last_hit_by = np.array([[1, 0]], dtype=np.uint8)
    seed = np.array([325637027], dtype=np.uint32)
    state_flags = np.zeros((n, 5), dtype=np.uint8)

    p0 = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=all_action_id[:, 0],
        action_frame_i16=all_action_frame[:, 0],
        ref_action_id_u16=all_ref_action_id[:, 0],
        on_ground_u8=all_on_ground[:, 0],
        hitlag_u16=all_hitlag[:, 0],
        hitstun_u16=all_hitstun[:, 0],
        state_flags_u8=state_flags,
        last_hit_by_u8=all_last_hit_by[:, 0],
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        all_ref_action_id_u16=all_ref_action_id,
        all_on_ground_u8=all_on_ground,
        all_hitlag_u16=all_hitlag,
        all_hitstun_u16=all_hitstun,
        all_last_hit_by_u8=all_last_hit_by,
        frame_pre_random_seed_u32=seed,
        damagefly_roll_prob=0.3,
        victim_port=0,
        num_players=2,
    )
    p1 = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=all_action_id[:, 1],
        action_frame_i16=all_action_frame[:, 1],
        ref_action_id_u16=all_ref_action_id[:, 1],
        on_ground_u8=all_on_ground[:, 1],
        hitlag_u16=all_hitlag[:, 1],
        hitstun_u16=all_hitstun[:, 1],
        state_flags_u8=state_flags,
        last_hit_by_u8=all_last_hit_by[:, 1],
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        all_ref_action_id_u16=all_ref_action_id,
        all_on_ground_u8=all_on_ground,
        all_hitlag_u16=all_hitlag,
        all_hitstun_u16=all_hitstun,
        all_last_hit_by_u8=all_last_hit_by,
        frame_pre_random_seed_u32=seed,
        damagefly_roll_prob=0.3,
        victim_port=1,
        num_players=2,
    )

    assert int(p1[0]) == 4
    assert int(p0[0]) == 1


def test_attackairn_pre_gate_backfill_stops_on_action_and_phase_boundaries() -> None:
    # Only the contiguous airborne AttackAirN/source episode may carry the replay-proven hidden
    # Fighter_8006CDA4 stream phase backward. Grounded/unrelated prior rows are not part of the
    # source episode even if the later target row proves a nonzero consume.
    n = 4
    source_port0 = np.array([[0, 1]] * n, dtype=np.uint8)
    action_id = np.array([24, 65, 65, 65], dtype=np.uint16)
    action_frame = np.array([3, 0, 1, 2], dtype=np.int16)
    ref_action_id = np.array([24, 65, 65, 91], dtype=np.uint16)
    on_ground = np.array([1, 0, 0, 0], dtype=np.uint8)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.zeros(n, dtype=np.uint16)
    all_action_id = np.zeros((n, 2), dtype=np.uint16)
    all_action_frame = np.zeros((n, 2), dtype=np.int16)
    all_ref_action_id = np.zeros((n, 2), dtype=np.uint16)
    all_on_ground = np.zeros((n, 2), dtype=np.uint8)
    all_hitlag = np.zeros((n, 2), dtype=np.uint16)
    all_hitstun = np.zeros((n, 2), dtype=np.uint16)
    all_last_hit_by = np.full((n, 2), 6, dtype=np.uint8)
    all_ref_last_hit_by = np.full((n, 2), 6, dtype=np.uint8)
    all_action_id[:, 0] = action_id
    all_action_frame[:, 0] = action_frame
    all_ref_action_id[:, 0] = ref_action_id
    all_on_ground[:, 0] = on_ground
    all_action_id[:, 1] = np.array([69, 69, 69, 69], dtype=np.uint16)
    all_action_frame[:, 1] = np.array([3, 4, 5, 6], dtype=np.int16)
    all_ref_action_id[:, 1] = all_action_id[:, 1]
    all_ref_last_hit_by[3, 0] = 1

    out = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        ref_action_id_u16=ref_action_id,
        on_ground_u8=on_ground,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        state_flags_u8=np.zeros((n, 5), dtype=np.uint8),
        last_hit_by_u8=all_last_hit_by[:, 0],
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        all_ref_action_id_u16=all_ref_action_id,
        all_on_ground_u8=all_on_ground,
        all_hitlag_u16=all_hitlag,
        all_hitstun_u16=all_hitstun,
        all_last_hit_by_u8=all_last_hit_by,
        all_ref_last_hit_by_u8=all_ref_last_hit_by,
        frame_pre_random_seed_u32=np.full(n, 6009, dtype=np.uint32),
        damagefly_roll_prob=0.3,
        victim_port=0,
        num_players=2,
    )

    assert out.tolist() == [0, 1, 1, 1]


def test_attackairn_pre_gate_backfill_does_not_carry_zero_consume_marker() -> None:
    # Marker 4 means the immediate target row uses the frame-start gate sample with zero pre-gate
    # consumes. It is not persistent hidden held-item/x197C state and must not backfill.
    n = 2
    source_port0 = np.array([[0, 1]] * n, dtype=np.uint8)
    all_action_id = np.array([[65, 69], [65, 69]], dtype=np.uint16)
    all_action_frame = np.array([[1, 5], [2, 6]], dtype=np.int16)
    all_ref_action_id = all_action_id.copy()
    all_ref_action_id[1, 0] = 91
    all_ref_last_hit_by = np.full((n, 2), 6, dtype=np.uint8)
    all_ref_last_hit_by[1, 0] = 1

    out = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=all_action_id[:, 0],
        action_frame_i16=all_action_frame[:, 0],
        ref_action_id_u16=all_ref_action_id[:, 0],
        on_ground_u8=np.zeros(n, dtype=np.uint8),
        hitlag_u16=np.zeros(n, dtype=np.uint16),
        hitstun_u16=np.zeros(n, dtype=np.uint16),
        state_flags_u8=np.zeros((n, 5), dtype=np.uint8),
        last_hit_by_u8=np.full(n, 6, dtype=np.uint8),
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        all_ref_action_id_u16=all_ref_action_id,
        all_on_ground_u8=np.zeros((n, 2), dtype=np.uint8),
        all_hitlag_u16=np.zeros((n, 2), dtype=np.uint16),
        all_hitstun_u16=np.zeros((n, 2), dtype=np.uint16),
        all_last_hit_by_u8=np.full((n, 2), 6, dtype=np.uint8),
        all_ref_last_hit_by_u8=all_ref_last_hit_by,
        frame_pre_random_seed_u32=np.full(n, 1, dtype=np.uint32),
        damagefly_roll_prob=1.0,
        victim_port=0,
        num_players=2,
    )

    assert out.tolist() == [0, 4]


def test_damageflytop_same_source_backfill_carries_zero_consume_gate_marker() -> None:
    # DamageFlyTop rollout seed continuity is different from AttackAirN pre-action continuity:
    # marker 4 still consumes no hidden RNG, but it carries source-proven gate-admission provenance
    # across the same-source DamageFlyTop hitstun segment so a rollout seeded before the hit can
    # reach the delayed ftCo_8008DCE0 DamageFlyRoll decision.
    n = 3
    source_port0 = np.array([[0, 1]] * n, dtype=np.uint8)
    action_id = np.array([90, 90, 90], dtype=np.uint16)
    action_frame = np.array([29, 30, 31], dtype=np.int16)
    ref_action_id = np.array([90, 90, 91], dtype=np.uint16)
    all_action_id = np.zeros((n, 2), dtype=np.uint16)
    all_action_frame = np.zeros((n, 2), dtype=np.int16)
    all_action_id[:, 0] = np.array([67, 67, 67], dtype=np.uint16)
    all_action_frame[:, 0] = np.array([3, 3, 3], dtype=np.int16)
    all_action_id[:, 1] = action_id
    all_action_frame[:, 1] = action_frame

    out = _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        ref_action_id_u16=ref_action_id,
        on_ground_u8=np.zeros(n, dtype=np.uint8),
        hitlag_u16=np.zeros(n, dtype=np.uint16),
        hitstun_u16=np.array([3, 2, 1], dtype=np.uint16),
        state_flags_u8=np.zeros((n, 5), dtype=np.uint8),
        last_hit_by_u8=np.zeros(n, dtype=np.uint8),
        all_source_port0_u8=source_port0,
        all_action_id_u16=all_action_id,
        all_action_frame_i16=all_action_frame,
        frame_pre_random_seed_u32=np.full(n, 1, dtype=np.uint32),
        damagefly_roll_prob=1.0,
        victim_port=1,
        num_players=2,
    )

    assert out.tolist() == [4, 4, 4]
