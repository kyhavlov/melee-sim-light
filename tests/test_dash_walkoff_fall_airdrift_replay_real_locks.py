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
def test_dash_walkoff_fall_airdrift_target_pm1_and_negative() -> None:
    # Replay-real lock for Dash -> Fall walk-off handoff:
    # - ft_800844EC routes Dash ground-loss into ftCo_Fall_Enter.
    # - ftCo_Fall_Enter immediately calls ftCommon_ClampAirDrift on the carried self_vel.x.
    # refs/melee/src/melee/ft/ft_081B.c::ft_800844EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_ClampAirDrift
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0

    _, ref_prev, out_prev = _run_one_step_row(dataset_path, 10160, p)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=p)

    _, ref_target, out_target = _run_one_step_row(dataset_path, 10161, p)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=p)
    assert int(ref_target["action_id"][p]) == 29  # Fall
    assert float(out_target["speed_air_x_self"][p]) == pytest.approx(
        float(ref_target["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out_target["pos_x"][p]) == pytest.approx(float(ref_target["pos_x"][p]), abs=1e-6)

    _, ref_next, out_next = _run_one_step_row(dataset_path, 10162, p)
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=p)
    assert float(out_next["speed_air_x_self"][p]) == pytest.approx(
        float(ref_next["speed_air_x_self"][p]), abs=1e-6
    )

    _, ref_neg, out_neg = _run_one_step_row(dataset_path, 10158, p)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=p)

