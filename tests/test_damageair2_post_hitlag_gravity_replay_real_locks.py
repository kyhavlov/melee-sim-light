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
def test_damageair2_post_hitlag_gravity_target_pm1_and_negative_are_strict() -> None:
    # Replay-real lock for airborne DamageAir2 post-hitlag gravity ownership:
    # - Common Damage states route through ftCo_Damage_Phys.
    # - Airborne and !x221C_b6 takes ft_80084DB0, which applies gravity before integration.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset artifacts")

    p = 1

    _, ref_hitlag, out_hitlag = _run_one_step_row(dataset_path, 9613, p)
    _assert_branch_identity_match_ref(out_row=out_hitlag, ref_row=ref_hitlag, p=p)
    assert float(out_hitlag["pos_x"][p]) == pytest.approx(float(ref_hitlag["pos_x"][p]), abs=1e-6)
    assert float(out_hitlag["pos_y"][p]) == pytest.approx(float(ref_hitlag["pos_y"][p]), abs=1e-6)

    _, ref_prev, out_prev = _run_one_step_row(dataset_path, 9614, p)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=p)
    assert float(out_prev["pos_x"][p]) == pytest.approx(float(ref_prev["pos_x"][p]), abs=1e-6)
    assert float(out_prev["pos_y"][p]) == pytest.approx(float(ref_prev["pos_y"][p]), abs=1e-6)
    assert float(out_prev["speed_x_attack"][p]) == pytest.approx(float(ref_prev["speed_x_attack"][p]), abs=1e-6)
    assert float(out_prev["speed_y_attack"][p]) == pytest.approx(float(ref_prev["speed_y_attack"][p]), abs=1e-6)
    assert float(out_prev["speed_y_self"][p]) == pytest.approx(float(ref_prev["speed_y_self"][p]), abs=1e-6)

    for rec in (9615, 9616):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=p)
        assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
        assert float(out_row["pos_y"][p]) == pytest.approx(float(ref_row["pos_y"][p]), abs=1e-6)
        assert float(out_row["speed_y_self"][p]) == pytest.approx(float(ref_row["speed_y_self"][p]), abs=1e-6)
