from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _run_rollout_window_rows


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
                "replays/validation/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
            ),
            record=10025,
            p=0,
            expected_state_flags_1=48,
            note="Fresh DamageFlyTop destination entry clears stale x221A_b5 while preserving hitlag and SDI bits",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
            ),
            record=4063,
            p=1,
            expected_state_flags_1=0,
            note="CapturePulledLw destination entry clears stale x221A_b5 carry from the prior grounded action",
        ),
        _Case(
            dataset_rel=(
                "replays/validation/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
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
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.record, case.p)
    assert int(out_target["state_flags"][case.p, 1]) == case.expected_state_flags_1, case.note
    assert int(ref_target["state_flags"][case.p, 1]) == case.expected_state_flags_1, case.note

    for rec in (case.record - 1, case.record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, case.p)
        assert int(out_row["action_id"][case.p]) == int(ref_row["action_id"][case.p]), case.note
        assert int(out_row["action_frame"][case.p]) == int(ref_row["action_frame"][case.p]), case.note
        assert int(out_row["state_flags"][case.p, 1]) == int(ref_row["state_flags"][case.p, 1]), case.note


@pytest.mark.integration
def test_sheik_attackhi4_smash_charge_reaches_script_hurt_state_rollout_lock() -> None:
    # Official-suite positive for the Sheik AttackHi4 timebase owner. The source boundary is the
    # character's decoded start_smash_charge script frame: Sheik AttackHi4 holds at frame 10, then
    # releasing A resumes to frame 11/12, where the script set_hurt_state commands publish
    # fp+0x221A_b5. This guards against the old Fox/Falco-shaped frame-2 bridge without adding a
    # state-flag runtime exception.
    #
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
    # refs/melee/src/melee/ft/ft_0DF0.c::ftCo_800DF0D0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B128
    # data/moves/sheik.json::moves.ftCo_SM_AttackHi4.events start_smash_charge/set_hurt_state
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "replays/validation/sheik/TenseSameHummingbird.slpz"
    if not dataset_path.exists():
        pytest.skip("missing local Sheik validation dataset")

    rows = _run_rollout_window_rows(
        dataset_path,
        start_record=8715,
        window_records=(9124, 9131, 9132, 9151, 9152, 9153),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    p = 0
    expected = {
        9124: (63, 3, 0),
        9131: (63, 10, 0),
        9132: (63, 10, 0),
        9151: (63, 10, 0),
        9152: (63, 11, 0),
        9153: (63, 12, 0x04),
    }
    for record, (action_id, action_frame, state_flags_221a_mask) in expected.items():
        ref_row, out_row = rows[record]
        assert int(ref_row["action_id"][p]) == int(out_row["action_id"][p]) == action_id
        assert int(ref_row["action_frame"][p]) == int(out_row["action_frame"][p]) == action_frame
        assert int(ref_row["state_flags"][p, 1] & 0x04) == state_flags_221a_mask
        assert int(out_row["state_flags"][p, 1] & 0x04) == state_flags_221a_mask
