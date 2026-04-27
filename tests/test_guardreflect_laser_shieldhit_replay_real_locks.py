from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    negative_record: int
    target_record: int
    p: int
    seed_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            negative_record=4042,
            target_record=4044,
            p=1,
            seed_action=16,
            note="AGN locomotion->GuardSetOff laser handoff",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            negative_record=3491,
            target_record=3493,
            p=0,
            seed_action=21,
            note="TBK locomotion->GuardSetOff laser handoff",
        ),
    ],
)
def test_locomotion_laser_guardsetoff_family_lock(case: _Case) -> None:
    # Replay-real lock for the kept locomotion -> GuardSetOff projectile-shield handoff:
    # - grounded locomotion shield admission can hand projectile contact directly to the regular
    #   shield-hit / GuardSetOff owner lane without exposing an intermediate replay-visible
    #   GuardReflect frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    p = case.p

    neg_seed, neg_out, neg_ref = _step_one_row(dataset_path, case.negative_record)
    assert int(neg_out["action_id"][p]) == int(neg_ref["action_id"][p]), case.note
    assert int(neg_out["action_frame"][p]) == int(neg_ref["action_frame"][p]), case.note
    assert int(neg_out["animation_index"][p]) == int(neg_ref["animation_index"][p]), case.note
    assert int(neg_out["hitlag"][p]) == int(neg_ref["hitlag"][p]) == 0, case.note
    assert int(neg_out["instance_id"][p]) == int(neg_ref["instance_id"][p]), case.note
    assert [int(x) for x in neg_out["state_flags"][p]] == [int(x) for x in neg_ref["state_flags"][p]]
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(neg_out["items"][0][field]) == int(neg_ref["items"][0][field]), case.note

    for record in (case.target_record - 1, case.target_record, case.target_record + 1):
        seed_t, out_t, ref_t = _step_one_row(dataset_path, record)

        if record == case.target_record:
            assert int(seed_t["action_id"][p]) == case.seed_action, case.note
            assert int(ref_t["action_id"][p]) == 181, case.note  # GuardSetOff
            assert int(ref_t["action_frame"][p]) == 0, case.note
            assert int(ref_t["animation_index"][p]) == 40, case.note  # GuardDamage
            assert int(ref_t["hitlag"][p]) == 3, case.note
            assert float(ref_t["shield_hp"][p]) < float(seed_t["shield_hp"][p]), case.note

            for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
                assert int(out_t[field][p]) == int(ref_t[field][p]), case.note
            assert float(out_t["shield_hp"][p]) == float(ref_t["shield_hp"][p]), case.note
            for field in ("exists", "type", "owner", "instance_id"):
                assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), case.note

            assert [int(x) for x in out_t["state_flags"][p]] == [int(x) for x in ref_t["state_flags"][p]], case.note
            continue

        assert int(out_t["action_id"][p]) == int(ref_t["action_id"][p]), case.note
        assert int(out_t["action_frame"][p]) == int(ref_t["action_frame"][p]), case.note
        assert int(out_t["animation_index"][p]) == int(ref_t["animation_index"][p]), case.note
        assert int(out_t["hitlag"][p]) == int(ref_t["hitlag"][p]), case.note
        assert int(out_t["instance_id"][p]) == int(ref_t["instance_id"][p]), case.note
        assert [int(x) for x in out_t["state_flags"][p]] == [int(x) for x in ref_t["state_flags"][p]]
        for field in ("exists", "type", "owner", "instance_id"):
            assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), case.note


@pytest.mark.integration
def test_landing_guardreflect_laser_no_contact_row_stays_replay_real() -> None:
    # Replay-real negative lock for the adjacent Landing -> GuardReflect no-contact family:
    # - Landing IASA also delegates to ftCo_80091A4C, but this row stays on the GuardReflect
    #   no-contact lane in replay; the incoming laser must not be forced onto GuardSetOff /
    #   shield-hit ownership.
    # - This keeps the fresh GuardReflect item-contact family scoped to true grounded locomotion
    #   pose owners until a landing-specific shield-pose source is extracted.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 132
    p = 1
    seed_t, out_t, ref_t = _step_one_row(dataset_path, record)

    assert int(seed_t["action_id"][p]) == 42  # Landing
    assert int(ref_t["action_id"][p]) == 182  # GuardReflect
    assert int(ref_t["action_frame"][p]) == -1
    assert int(ref_t["hitlag"][p]) == 0

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), dataset_rel

    for slot in (0, 1):
        for field in ("exists", "type", "owner", "instance_id"):
            assert int(out_t["items"][slot][field]) == int(ref_t["items"][slot][field]), (
                f"{dataset_rel}: slot={slot} field={field}"
            )


@pytest.mark.integration
def test_dash_91ad8_guardon_defers_same_step_laser_shield_contact() -> None:
    # Replay-real lock for Dash_IASA's mid guard helper:
    # - `dash.x4 != 0 && cur_anim_frame <= x44` stays in the early Dash_IASA branch and must not
    #   enter GuardReflect.
    # - the following mid branch enters GuardOn through ftCo_80091AD8 -> ftCo_800923B4, but item
    #   collision for that frame has already passed, so the laser shield hit appears one row later.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_800923B4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1

    seed_733, out_733, ref_733 = _step_one_row(dataset_path, 733)
    assert int(seed_733["action_id"][p]) == 20  # Dash
    assert int(seed_733["dash_x4"][p]) == 1
    assert int(ref_733["action_id"][p]) == 20
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_733[field][p]) == int(ref_733[field][p]), field
    assert [int(x) for x in out_733["state_flags"][p]] == [
        int(x) for x in ref_733["state_flags"][p]
    ]

    seed_734, out_734, ref_734 = _step_one_row(dataset_path, 734)
    assert int(seed_734["action_id"][p]) == 20  # Dash
    assert int(ref_734["action_id"][p]) == 178  # GuardOn
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_734[field][p]) == int(ref_734[field][p]), field
    assert [int(x) for x in out_734["state_flags"][p]] == [
        int(x) for x in ref_734["state_flags"][p]
    ]

    _, out_735, ref_735 = _step_one_row(dataset_path, 735)
    assert int(ref_735["action_id"][p]) == 181  # GuardSetOff
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_735[field][p]) == int(ref_735[field][p]), field


@pytest.mark.integration
def test_dash_x4_clear_guardon_can_take_same_step_laser_shield_contact() -> None:
    # Boundary control for the Dash_IASA x4 split:
    # - Dash `x4=0` reaches the mid `ftCo_80091AD8` helper directly and can still expose same-step
    #   laser shield contact.
    # - The same-step deferral is limited to the `x4 != 0` early-branch handoff repaired above.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    seed_t, out_t, ref_t = _step_one_row(dataset_path, 773)
    assert int(seed_t["action_id"][p]) == 20  # Dash
    assert int(seed_t["dash_x4"][p]) == 0
    assert int(seed_t["action_frame"][p]) > 4
    assert int(ref_t["action_id"][p]) == 181  # GuardSetOff
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
    assert [int(x) for x in out_t["state_flags"][p]] == [
        int(x) for x in ref_t["state_flags"][p]
    ]


@pytest.mark.integration
@pytest.mark.parametrize("record", [117, 258])
def test_dash_91ad8_guardreflect_controls_still_reflect(record: int) -> None:
    # Negative controls: not every fresh shield input from Dash is suppressed. Dash rows outside
    # the early `dash.x4` branch still reach GuardReflect through the decomp guard helper.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_t, out_t, ref_t = _step_one_row(dataset_path, record)
    assert int(seed_t["action_id"][p]) == 20  # Dash
    assert int(ref_t["action_id"][p]) == 182  # GuardReflect
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun"):
        assert int(out_t[field][p]) == int(ref_t[field][p]), field
