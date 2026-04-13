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

            for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
                assert int(out_t[field][p]) == int(ref_t[field][p]), case.note
            for field in ("exists", "type", "owner", "instance_id"):
                assert int(out_t["items"][0][field]) == int(ref_t["items"][0][field]), case.note

            if case.dataset_rel.endswith("AttachedGoodNaturedGuanaco.msl"):
                assert [int(x) for x in out_t["state_flags"][p]] == [20, 33, 128, 96, 0], case.note
                assert [int(x) for x in ref_t["state_flags"][p]] == [4, 33, 128, 96, 0], case.note
            else:
                assert [int(x) for x in out_t["state_flags"][p]] == [84, 33, 128, 96, 0], case.note
                assert [int(x) for x in ref_t["state_flags"][p]] == [68, 33, 128, 96, 0], case.note
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
