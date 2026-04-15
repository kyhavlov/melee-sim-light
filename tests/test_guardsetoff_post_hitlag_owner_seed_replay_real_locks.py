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
    expected_ref_action_frame: int
    expected_ref_state_flags_3: int
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
            expected_ref_action_frame=2,
            expected_ref_state_flags_3=0,
            note="AGN blocker A is a normal GuardSetOff post-hitlag handoff with no powershield-active owner",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1477,
            p=0,
            expected_owner=1,
            expected_target_plus1_owner=1,
            expected_ref_action_frame=3,
            expected_ref_state_flags_3=0,
            note="AGN blocker B shares the same normal GuardSetOff post-hitlag owner class",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2393,
            p=0,
            expected_owner=2,
            expected_target_plus1_owner=2,
            expected_ref_action_frame=6,
            expected_ref_state_flags_3=32,
            note="AGN blocker C is a powershield-active GuardSetOff post-hitlag handoff",
        ),
    ],
)
def test_guardsetoff_post_hitlag_owner_seed_locks_blockers_and_controls(case: _Case) -> None:
    # Foundational F02 owner lock:
    # - ftCo_GuardSetOff_Anim owns the last-hitlag / first-post-hitlag handoff rows.
    # - ftCo_80093BC0 can simultaneously keep the powershield-active x221C_b2 lane alive.
    # - The general frame_speed_mul_f32 lane stays causal; exact parity for hidden x19A4 /
    #   lightshield-owned exit rates comes from the explicit GuardSetOff-only
    #   guard_setoff_exit_frame_speed_mul_f32 lane.
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
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note
    assert int(out_target["action_frame"][p]) == int(ref_target["action_frame"][p]), case.note
    assert int(ref_target["state_flags"][p, 3]) == case.expected_ref_state_flags_3, case.note
    assert int(out_target["state_flags"][p, 3]) == int(ref_target["state_flags"][p, 3]), case.note


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


@pytest.mark.integration
def test_guardsetoff_exit_frame_speed_lane_does_not_weaken_causal_frame_speed() -> None:
    # This row needs the explicit GuardSetOff exit-rate lane: the strictly causal
    # frame_speed_mul_f32 seed is the best current/past reconstruction, while the exact hidden
    # x19A4/lightshield-owned rate is only replay-visible on the next non-hitlag GuardSetOff row.
    # Runtime must consume the explicit GuardSetOff lane and leave frame_speed_mul_f32 causal.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 4047
    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][p]) == 181
    assert int(seed["hitlag"][p]) == 1
    assert int(seed["guard_setoff_hitlag_exit_phase_u8"][p]) == 2
    assert float(seed["frame_speed_mul_f32"][p]) == pytest.approx(5.868613243103027)
    assert float(seed["guard_setoff_exit_frame_speed_mul_f32"][p]) == pytest.approx(6.931034564971924)

    _, ref_target, out_target = _run_one_step_row(dataset_path, record, p)
    assert int(ref_target["action_frame"][p]) == 6
    assert int(out_target["action_frame"][p]) == int(ref_target["action_frame"][p])
