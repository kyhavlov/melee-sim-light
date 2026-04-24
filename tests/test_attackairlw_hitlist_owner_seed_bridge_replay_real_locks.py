from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_identity_lock_fields_match_ref,
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_attackairlw_hitlist_owner_seed_bridge_target_pm1_both_players_strict_lock() -> None:
    # Replay-real target+/-1 lock for the AttackAirLw stale-owner hitlist bridge lane.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # - data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    target_record = 7512
    rows = (target_record - 1, target_record, target_record + 1)
    p_target = 0

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == 24  # Dash
    assert int(target["seed_t"]["hitlag"][p_target]) == 0
    assert int(target["seed_t"]["hitstun"][p_target]) == 0
    assert int(target["ref_t1"]["action_id"][p_target]) == 86  # DamageFlyHi
    assert int(target["ref_t1"]["hitlag"][p_target]) > 0
    assert int(target["ref_t1"]["hitstun"][p_target]) > 0
    # Attacker context row: AttackAirLw owner on the opposing player.
    assert int(target["seed_t"]["action_id"][1]) == 69  # AttackAirLw

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p_attacker", "p_defender", "attacker_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            3630,
            1,
            0,
            69,  # AttackAirLw
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            1206,
            0,
            1,
            69,  # AttackAirLw
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.msl",
            3276,
            0,
            1,
            67,  # AttackAirB
        ),
    ],
)
def test_attackair_replay_only_shield_admission_valid_empty_hitcapsule_locks(
    dataset_rel: str, record: int, p_attacker: int, p_defender: int, attacker_action: int
) -> None:
    # Replay-real locks for the non-causal per-HitCapsule shield-admission seed lane:
    # - The dense group seed carries an indefinite victim latch that would suppress the live
    #   shield hit.
    # - `combat_hitlist_hb_valid` marks the active HitCapsules authoritative-empty for the
    #   teacher-forced seed frame, so runtime uses the decomp-shaped HitCapsule lane instead of
    #   the coarse group fallback.
    # - This is a seed provenance lane, not a runtime row bridge; normal rollouts carry the
    #   HitCapsule rings directly.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAir*.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (record - 1, record, record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[record]
    seed_t = target["seed_t"]
    ref_t1 = target["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == int(attacker_action)
    assert int(seed_t["hitlag"][p_attacker]) == 0
    assert int(seed_t["hitlag"][p_defender]) == 0
    assert int(ref_t1["action_id"][p_defender]) == 181  # GuardSetOff
    assert int(ref_t1["hitlag"][p_attacker]) > 0
    assert int(ref_t1["hitlag"][p_defender]) > 0
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) in (0, 0xFFFF)
    assert any(int(v) != 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    for hb in range(4):
        if int(seed_t["combat_hitlist_hb_valid"][p_attacker, hb]) != 0:
            assert all(int(x) == 0 for x in seed_t["combat_hitlist_hb_cd"][p_attacker, hb, :2])

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_defender)
        for p in (p_attacker, p_defender):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
def test_attackairlw_guard_shield_admission_negative_neighbor_stays_suppressed() -> None:
    # Negative adjacent lock for GAT:3629:
    # - This is the same AttackAirLw/Guard neighborhood as GAT:3630, but the replay does not enter
    #   GuardSetOff and the dense hitlist latch must remain suppressing.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3629
    p_attacker = 1
    p_defender = 0
    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == 69  # AttackAirLw
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) == 0xFFFF
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    assert int(ref_t1["action_id"][p_defender]) == 178  # GuardOn stays held
    assert int(ref_t1["hitlag"][p_defender]) == 0
    assert int(ref_t1["hitlag"][p_attacker]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    for p in (p_attacker, p_defender):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )


@pytest.mark.integration
def test_attackairb_guard_shield_admission_negative_neighbor_stays_suppressed() -> None:
    # Negative adjacent lock for HVG:3275:
    # - AttackAirB carries the same stale dense hitlist shape as HVG:3276, but the replay-visible
    #   next row does not enter GuardSetOff/hitlag.
    # - The replay-only authoritative-empty per-HitCapsule lane must therefore stay off.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HilariousVillainousGiraffe.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3275
    p_attacker = 0
    p_defender = 1
    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == 67  # AttackAirB
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) == 0xFFFF
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    assert int(ref_t1["action_id"][p_defender]) == 179  # Guard stays held
    assert int(ref_t1["hitlag"][p_defender]) == 0
    assert int(ref_t1["hitlag"][p_attacker]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    for p in (p_attacker, p_defender):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )
