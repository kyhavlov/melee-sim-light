from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def _assert_branch_identity_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


@pytest.mark.integration
def test_cliffwait_handoff_snap_target_pm1_and_negative() -> None:
    # Replay-real lock for CliffCatch -> CliffWait same-proc snap ownership:
    # - ftCo_CliffCatch_Anim enters CliffWait through ftCo_8009A804.
    # - Fighter_procUpdate then runs ftCo_CliffWait_Phys, which is a direct call-through to
    #   ftCo_CliffCatch_Phys on the transition row.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::{ftCo_8009A804,ftCo_CliffWait_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0

    _, ref_prev, out_prev = _run_one_step_row(dataset_path, 2597, p)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=p)
    assert float(out_prev["pos_x"][p]) == pytest.approx(float(ref_prev["pos_x"][p]), abs=1e-6)

    _, ref_target, out_target = _run_one_step_row(dataset_path, 2598, p)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=p)
    assert int(ref_target["action_id"][p]) == 253  # CliffWait
    assert float(out_target["pos_x"][p]) == pytest.approx(float(ref_target["pos_x"][p]), abs=1e-6)
    assert float(out_target["pos_y"][p]) == pytest.approx(float(ref_target["pos_y"][p]), abs=1e-6)

    _, ref_next, out_next = _run_one_step_row(dataset_path, 2599, p)
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=p)
    assert float(out_next["pos_x"][p]) == pytest.approx(float(ref_next["pos_x"][p]), abs=1e-6)

    _, ref_neg, out_neg = _run_one_step_row(dataset_path, 2600, p)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=p)
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)
