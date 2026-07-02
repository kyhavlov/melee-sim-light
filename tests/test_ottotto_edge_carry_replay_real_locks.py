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
def test_ottotto_edge_wait_dash_carry_target_pm1_and_negative() -> None:
    # Replay-real lock for edge-owned Ottotto admission / same-facing Dash follow-up:
    # - ftCo_8009A3C8 enters Ottotto on Collide_Edge.
    # - ftCo_Ottotto_IASA then routes through ftCo_Dash_CheckInput for the same-facing Dash follow-up.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_Ottotto_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    p = 0

    _, ref_prev, out_prev = _run_one_step_row(dataset_path, 10158, p)
    _assert_branch_identity_match_ref(out_row=out_prev, ref_row=ref_prev, p=p)

    _, ref_target, out_target = _run_one_step_row(dataset_path, 10159, p)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=p)
    assert int(ref_target["action_id"][p]) == 245  # Ottotto
    assert float(out_target["pos_x"][p]) == pytest.approx(float(ref_target["pos_x"][p]), abs=1e-6)
    assert float(out_target["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_target["speed_ground_x_self"][p]), abs=1e-6
    )

    _, ref_next, out_next = _run_one_step_row(dataset_path, 10160, p)
    _assert_branch_identity_match_ref(out_row=out_next, ref_row=ref_next, p=p)
    assert int(ref_next["action_id"][p]) == 20  # Dash
    assert float(out_next["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_next["speed_ground_x_self"][p]), abs=1e-6
    )

    _, ref_neg, out_neg = _run_one_step_row(dataset_path, 10162, p)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=p)
    assert int(ref_neg["action_id"][p]) == 29  # Fall
