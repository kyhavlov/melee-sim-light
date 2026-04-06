from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    expected_state_flags_1: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=10025,
            p=0,
            expected_state_flags_1=48,
            note="Fresh DamageFlyTop destination entry clears stale x221A_b5 while preserving hitlag and SDI bits",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
            ),
            record=4063,
            p=1,
            expected_state_flags_1=0,
            note="CapturePulledLw destination entry clears stale x221A_b5 carry from the prior grounded action",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
            ),
            record=5369,
            p=1,
            expected_state_flags_1=0,
            note="CapturePulledLw entry from KneeBend also clears stale x221A_b5 carry",
        ),
    ],
)
def test_state_flags_221a_b5_entry_clears_replay_real_locks(case: _Case) -> None:
    # Decomp refs:
    # - x221A_b5 mirrors whole/non-enabled hurt-capsule state in ftColl_8007B0C0 / ftColl_8007B128.
    # - Fresh Damage* entries run through Fighter_ChangeMotionState reset before the new hitlag
    #   window is observed.
    # - CapturePulled/Wait/Cut motion states use ftCo_MF_Capture, which does not keep colanim hit
    #   status across the destination entry.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B0C0,ftColl_8007B128}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::{ftCo_MF_CatchWait,ftCo_MF_Capture}
    # refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_CapturePulledHi,ftCo_MS_CaptureWaitHi,
    #   ftCo_MS_CapturePulledLw,ftCo_MS_CaptureWaitLw,ftCo_MS_CaptureCut}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.record, case.p)
    assert int(out_target["state_flags"][case.p, 1]) == case.expected_state_flags_1, case.note
    assert int(ref_target["state_flags"][case.p, 1]) == case.expected_state_flags_1, case.note

    for rec in (case.record - 1, case.record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, case.p)
        assert int(out_row["action_id"][case.p]) == int(ref_row["action_id"][case.p]), case.note
        assert int(out_row["action_frame"][case.p]) == int(ref_row["action_frame"][case.p]), case.note
        assert int(out_row["state_flags"][case.p, 1]) == int(ref_row["state_flags"][case.p, 1]), case.note
