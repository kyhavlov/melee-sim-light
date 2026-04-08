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
def test_catchpull_ground_speed_owner_target_pm1_and_negative() -> None:
    # Replay-real lock for Catch/CatchDash connect owner ground-speed ownership:
    # - fn_800D9CE8 writes 0 to fp->gr_vel before entering CatchPull/CatchDashPull.
    # - The post-frame CatchPull/CatchDashPull owner should not retain the pre-connect grounded
    #   slide speed on the connect row.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    gat = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not gat.exists():
        pytest.skip(f"missing local dataset: {gat}")

    owner = 0

    # target-1: Catch startup still active; branch identity only.
    _, ref_prev, out_prev = _run_one_step_row(gat, 10710, owner)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=owner)
    assert int(ref_prev["action_id"][owner]) == 212  # Catch

    # target: Catch connect -> CatchPull zeroes ground speed.
    seed_target, ref_target, out_target = _run_one_step_row(gat, 10711, owner)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=owner)
    assert int(ref_target["action_id"][owner]) == 213  # CatchPull
    assert float(out_target["speed_ground_x_self"][owner]) == pytest.approx(
        float(ref_target["speed_ground_x_self"][owner]), abs=1e-6
    )
    assert abs(float(out_target["pos_x"][owner]) - float(ref_target["pos_x"][owner])) < abs(
        float(seed_target["pos_x"][owner]) - float(ref_target["pos_x"][owner])
    )

    # target+1: grounded CatchPull continuation stays zeroed.
    _, ref_next, out_next = _run_one_step_row(gat, 10712, owner)
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=owner)
    assert float(out_next["speed_ground_x_self"][owner]) == pytest.approx(
        float(ref_next["speed_ground_x_self"][owner]), abs=1e-6
    )

    # Explicit negative: non-connect Catch frame should not be forced to zero.
    _, ref_neg, out_neg = _run_one_step_row(gat, 10709, owner)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=owner)
    assert int(ref_neg["action_id"][owner]) == 212  # Catch
    assert float(out_neg["speed_ground_x_self"][owner]) != pytest.approx(0.0, abs=1e-6)
