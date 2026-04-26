from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _DashIasaTurnCase:
    dataset_rel: str
    record: int
    player: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DashIasaTurnCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PutridJoyousOryx.msl"
            ),
            record=4828,
            player=0,
            note="PJO Dash opposite-flick Turn handoff",
        ),
        _DashIasaTurnCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.msl"
            ),
            record=8290,
            player=1,
            note="MJA Dash opposite-flick Turn handoff",
        ),
        _DashIasaTurnCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=4230,
            player=0,
            note="BHH Dash opposite-flick Turn handoff",
        ),
    ],
)
def test_dash_iasa_turn_terminal_friction_target_pm1_lock(case: _DashIasaTurnCase) -> None:
    # Replay-real lock for Dash IASA's opposite-flick Turn path:
    # - ftCo_Dash_IASA can enter Turn via ftCo_Dash_CheckInput/ftCo_Turn_Enter_Smash.
    # - That path still falls through to Dash IASA's terminal gr_vel scalar
    #   (`gr_vel -= gr_vel * p_ftCommonData->x54 * ft_GetGroundFrictionMultiplier(fp)`).
    # - The newly entered Turn state's Phys callback then applies ordinary ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    samples = read_dataset(str(dataset_path)).samples
    for rec in (case.record - 1, case.record, case.record + 1):
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[case.record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    p = case.player
    assert int(seed["action_id"][p]) == 20, case.note  # Dash
    assert int(ref["action_id"][p]) == 18, case.note  # Turn

    for rec in (case.record - 1, case.record, case.record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=rec,
            p=p,
        )


def test_dash_iasa_guardreflect_terminal_friction_bhh_lock() -> None:
    # Same Dash IASA terminal scalar, but for the non-returning powershield admission path:
    # ftCo_Dash_IASA -> ftCo_80091A4C/ftCo_80091AD8 -> ftCo_80093A50 still falls through to
    # `gr_vel -= gr_vel * p_ftCommonData->x54 * ft_GetGroundFrictionMultiplier(fp)`.
    # BHH rec241 exposed this because the Dash->GuardReflect velocity must be reduced before
    # GuardReflect_Phys applies ordinary ground friction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80093A50}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    record = 241
    p = 1
    samples = read_dataset(str(dataset_path)).samples
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(ref["action_id"][p]) == 182  # GuardReflect

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
    assert float(out_row["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out_row["speed_air_x_self"][p]) == pytest.approx(
        float(ref_row["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out_row["pos_x"][p]) == pytest.approx(float(ref_row["pos_x"][p]), abs=1e-6)
