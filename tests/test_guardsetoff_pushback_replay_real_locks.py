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
def test_guardsetoff_ground_push_target_pm1_and_negative() -> None:
    # Replay-real lock for grounded GuardSetOff pushback ownership:
    # - ftColl_80076CBC writes x19A4/specialn_facing_dir on shield contact.
    # - ftCo_80092F2C computes the grounded GuardSetOff push and writes fp->gr_vel.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    target_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    negative_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not target_path.exists() or not negative_path.exists():
        pytest.skip("missing local dataset artifacts")

    p = 1

    _, ref_prev, out_prev = _run_one_step_row(target_path, 9106, p)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=p)

    seed_target, ref_target, out_target = _run_one_step_row(target_path, 9107, p)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=p)
    assert int(ref_target["action_id"][p]) == 181  # GuardSetOff
    assert float(out_target["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_target["speed_ground_x_self"][p]), abs=1e-6
    )
    assert abs(float(out_target["speed_ground_x_self"][p]) - float(ref_target["speed_ground_x_self"][p])) < abs(
        float(seed_target["speed_ground_x_self"][p]) - float(ref_target["speed_ground_x_self"][p])
    )
    assert float(out_target["speed_air_x_self"][p]) == pytest.approx(
        float(ref_target["speed_air_x_self"][p]), abs=1e-6
    )

    _, ref_next, out_next = _run_one_step_row(target_path, 9108, p)
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=p)
    assert float(out_next["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_next["speed_ground_x_self"][p]), abs=1e-6
    )

    _, ref_neg, out_neg = _run_one_step_row(negative_path, 8305, p)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=p)
    assert float(out_neg["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_neg["speed_ground_x_self"][p]), abs=1e-6
    )
