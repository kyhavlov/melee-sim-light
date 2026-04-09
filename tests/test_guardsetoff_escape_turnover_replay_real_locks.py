from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


def _assert_branch_identity_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


@pytest.mark.integration
def test_guardsetoff_escape_turnover_window_is_replay_exact() -> None:
    # Replay-real lock for the GuardSetOff->EscapeN turnover drift:
    # - ftCommon_8007E0E4 / ftCommon_8007DD7C apply grounded fighter-overlap nudge via
    #   `p_ftCommonData->x450` before Fighter_procUpdate position integration.
    # - This exact window exercises the missing horizontal nudge during steady GuardSetOff and the
    #   same-proc GuardSetOff->EscapeN turnover.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset artifacts")

    p = 0
    for rec in (9414, 9415, 9416, 9417, 9418):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
        assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
        assert float(out_row["pos_y"][p]) == pytest.approx(float(ref_row["pos_y"][p]), abs=1e-6)
