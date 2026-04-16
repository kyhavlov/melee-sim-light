from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _run_pre_combat_debug_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_f08b_body_contact_geometry_bhh1163_turn_internal_facing_positive_lock() -> None:
    # F08b BODY collision-space positive lock:
    # - Replay proves an AttackAirB BODY hit at t+1: defender percent increases, both fighters
    #   enter hitlag, and source identity points at the attacker.
    # - The sim must place the defender Turn hurtcaps using ftCo_Turn's internal has_turned
    #   orientation, admit the normal BODY overlap, and then run ftColl_80076ED8 followup.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #     ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1163
    attacker = 0
    defender = 1
    hitbox_id = 2

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 18  # Turn
    assert int(seed_t["turn_has_turned"][defender]) == 1
    assert int(ref_t1["action_id"][defender]) == 76  # DamageHi2
    assert float(ref_t1["percent"][defender]) > float(seed_t["percent"][defender])
    assert int(ref_t1["hitlag"][attacker]) > 0
    assert int(ref_t1["hitlag"][defender]) > 0
    assert int(ref_t1["last_hit_by"][defender]) == attacker
    assert int(ref_t1["instance_hit_by"][defender]) == int(seed_t["instance_id"][attacker])

    _, contacts, _, timing = _run_pre_combat_debug_row(dataset_path, record, attacker, hitbox_id)
    assert int(timing["msid"]) == 70  # ftCo_SM_AttackAirB
    assert int(timing["enabled_cur"]) == 1
    body_contacts = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == hitbox_id
        and int(c["contact_kind"]) == 0
    ]
    assert body_contacts, "expected Turn internal-facing hurtcaps to admit the BODY overlap"

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for p in (attacker, defender):
        for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
            assert int(out_row[field][p]) == int(ref_row[field][p])
    for field in ("percent",):
        assert float(out_row[field][defender]) == pytest.approx(float(ref_row[field][defender]))
    assert int(out_row["last_hit_by"][defender]) == attacker
    assert int(out_row["instance_hit_by"][defender]) == int(seed_t["instance_id"][attacker])


@pytest.mark.integration
def test_f08b_body_contact_geometry_bhh1169_turn_hitlist_continuation_stays_suppressed() -> None:
    # Followup lock for the same accepted BODY hit:
    # - BHH:1163 admits the first AttackAirB BODY hit.
    # - Later reseeded rows must carry the HitCapsule victim list created by that accepted hit,
    #   so the still-active AttackAirB capsule does not re-hit after hitlag exits.
    #
    # Seed-history owner:
    # - runtime advances anim_frame_f32 before hitbox/hurtcap refresh;
    # - ftColl_80076ED8 registers the victim after BODY damage admission;
    # - lbColl_8000ACFC suppresses subsequent frames while the victim is present.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1169
    attacker = 0
    defender = 1

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]

    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 76  # DamageHi2
    assert int(seed_t["combat_hitlist_cd"][attacker, 0, defender]) != 0
    assert int(ref_t1["hitlag"][attacker]) == 0
    assert int(ref_t1["hitlag"][defender]) == 0
    assert float(ref_t1["percent"][defender]) == pytest.approx(float(seed_t["percent"][defender]))

    for p in (attacker, defender):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "hitlag",
            "hitstun",
            "last_hit_by",
            "instance_hit_by",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p])
        assert float(out_row["percent"][p]) == pytest.approx(float(ref_row["percent"][p]))


@pytest.mark.integration
def test_f08b_body_contact_geometry_cardinal_attackairb_turn_control_stays_suppressed() -> None:
    # Negative same-shape control:
    # Cardinal TBK:5523 also has a steady AttackAirB attacker and grounded Turn defender, but replay
    # has no BODY hit on this frame. The Turn internal-facing owner must not become a broad
    # AttackAirB/Turn admission bridge, especially before ftCo_Turn has flipped internally.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #     ftCo_Turn_Enter,ftCo_Turn_Anim_Inner}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 5523
    attacker = 0
    defender = 1
    hitbox_id = 2

    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    assert int(seed_t["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed_t["action_id"][defender]) == 18  # Turn
    assert int(seed_t["turn_has_turned"][defender]) == 0

    _, contacts, _, _ = _run_pre_combat_debug_row(dataset_path, record, attacker, hitbox_id)
    body_contacts = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == hitbox_id
        and int(c["contact_kind"]) == 0
    ]
    assert not body_contacts, "pre-turn-facing BODY overlap must remain suppressed"

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    assert int(ref_row["hitlag"][defender]) == 0
    assert int(out_row["hitlag"][defender]) == 0
    assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender])
    assert float(out_row["percent"][defender]) == pytest.approx(float(ref_row["percent"][defender]))
