from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_owner: int
    expected_target_plus1_owner: int
    expected_out_action_frame: int
    expected_ref_action_frame: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1212,
            p=1,
            expected_owner=1,
            expected_target_plus1_owner=1,
            expected_out_action_frame=1,
            expected_ref_action_frame=2,
            note="AGN blocker A is a normal GuardSetOff post-hitlag handoff with no powershield-active owner",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1477,
            p=0,
            expected_owner=1,
            expected_target_plus1_owner=1,
            expected_out_action_frame=1,
            expected_ref_action_frame=3,
            note="AGN blocker B shares the same normal GuardSetOff post-hitlag owner class",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2393,
            p=0,
            expected_owner=2,
            expected_target_plus1_owner=2,
            expected_out_action_frame=10,
            expected_ref_action_frame=6,
            note="AGN blocker C is a powershield-active GuardSetOff post-hitlag handoff",
        ),
    ],
)
def test_guardsetoff_post_hitlag_owner_seed_locks_blockers_and_controls(case: _Case) -> None:
    # Foundational F02 blocker lock:
    # - ftCo_GuardSetOff_Anim owns the last-hitlag / first-post-hitlag handoff rows.
    # - ftCo_80093BC0 can simultaneously keep the powershield-active x221C_b2 lane alive.
    # - This seed lane distinguishes those two owner classes without changing runtime behavior.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    assert int(samples[case.target_record - 2]["seed_t"]["guard_setoff_post_hitlag_owner_u8"][p]) == 0, case.note
    assert int(samples[case.target_record - 1]["seed_t"]["guard_setoff_post_hitlag_owner_u8"][p]) == 0, case.note
    assert int(samples[case.target_record]["seed_t"]["guard_setoff_post_hitlag_owner_u8"][p]) == case.expected_owner, case.note
    assert int(samples[case.target_record + 1]["seed_t"]["guard_setoff_post_hitlag_owner_u8"][p]) == case.expected_target_plus1_owner, case.note
    assert int(samples[case.target_record + 2]["seed_t"]["guard_setoff_post_hitlag_owner_u8"][p]) == 0, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_target["action_frame"][p]) == case.expected_out_action_frame, case.note
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note


@pytest.mark.integration
def test_guardsetoff_post_hitlag_owner_seed_negative_control_outside_handoff() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1214]["seed_t"]
    assert int(seed["guard_setoff_post_hitlag_owner_u8"][1]) == 0
