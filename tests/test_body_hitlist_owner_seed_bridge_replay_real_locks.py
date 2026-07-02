from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_identity_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


_CARDINAL = "replays/validation/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p_attacker", "p_defender", "attacker_action"),
    [
        ("QuerulousGrandDinosaur.slpz", 5868, 1, 0, 356),  # Falco Shine BODY hit.
        ("GracefulAttachedTurtle.slpz", 9619, 0, 1, 69),  # AttackAirLw BODY continuation.
        ("TreasuredBackKangaroo.slpz", 1848, 0, 1, 69),  # AttackAirLw BODY continuation.
    ],
)
def test_replay_only_body_admission_valid_empty_hitcapsule_locks(
    dataset_name: str, record: int, p_attacker: int, p_defender: int, attacker_action: int
) -> None:
    # Replay-real locks for the non-causal per-HitCapsule BODY-admission seed lane:
    # - Dense group state still carries the victim and would suppress the live BODY hit.
    # - `combat_hitlist_hb_valid` marks the active HitCapsules authoritative-empty only when the
    #   next post-frame proves fighter BODY damage: percent increase, both fighters in hitlag, and
    #   source-owner attribution to the current attacker.
    # - This is a seed provenance lane, not a runtime clear; normal rollouts carry HitCapsule rings.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CARDINAL / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (record - 1, record, record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    seed_t = samples[record]["seed_t"]
    ref_t1 = samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == int(attacker_action)
    assert int(seed_t["hitlag"][p_attacker]) == 0
    assert int(seed_t["hitlag"][p_defender]) == 0
    assert int(ref_t1["hitlag"][p_attacker]) > 0
    assert int(ref_t1["hitlag"][p_defender]) > 0
    assert float(ref_t1["percent"][p_defender]) > float(seed_t["percent"][p_defender])
    assert int(ref_t1["last_hit_by"][p_defender]) == int(p_attacker)
    assert int(ref_t1["instance_hit_by"][p_defender]) == int(seed_t["instance_id"][p_attacker])
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
@pytest.mark.parametrize(
    ("dataset_name", "record", "p_attacker", "p_defender", "reason"),
    [
        ("QuerulousGrandDinosaur.slpz", 8638, 0, 1, "phantom/no-percent BODY contact"),
        ("TreasuredBackKangaroo.slpz", 5247, 1, 0, "extra BODY hit outside stale-admission owner"),
    ],
)
def test_replay_only_body_admission_negatives_keep_hitcapsules_non_authoritative(
    dataset_name: str, record: int, p_attacker: int, p_defender: int, reason: str
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _CARDINAL / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed_t = ds.rows[record]["seed_t"]
    ref_t1 = ds.rows[record]["ref_t1"]
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker]), reason
    assert not (
        float(ref_t1["percent"][p_defender]) > float(seed_t["percent"][p_defender])
        and int(ref_t1["hitlag"][p_attacker]) > 0
        and int(ref_t1["hitlag"][p_defender]) > 0
        and int(ref_t1["last_hit_by"][p_defender]) == int(p_attacker)
        and int(ref_t1["instance_hit_by"][p_defender]) == int(seed_t["instance_id"][p_attacker])
    ), reason
