from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_damagefly_downbound_state_selection_regression import _run_one_step_with_rollout
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _DamageLandRootYCase:
    dataset_rel: str
    target_record: int
    p: int
    expected_seed_action: int
    expected_ref_action: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageLandRootYCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=10670,
            p=1,
            expected_seed_action=90,
            expected_ref_action=201,
            note="DamageFlyLw -> PassiveStandB should snap root Y to the floor line on the contact frame",
        ),
        _DamageLandRootYCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=5063,
            p=0,
            expected_seed_action=91,
            expected_ref_action=199,
            note="DamageFlyTop -> Passive should inherit the callback-owned floor-root position",
        ),
        _DamageLandRootYCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=9985,
            p=0,
            expected_seed_action=86,
            expected_ref_action=42,
            note="DamageAir3 -> Landing should project the grounded root to the active floor line",
        ),
    ],
)
def test_damage_land_root_y_target_pm1_replay_real(case: _DamageLandRootYCase) -> None:
    # Decomp ownership:
    # - Damage/DamageFly collision resolves grounded contact before ftCo_80090184 /
    #   ftCo_Landing_Enter_Basic select the grounded destination state.
    # - mpLib_8004DD90_Floor projects to the floor line with the +0.0001 landing bias.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_Coll,ftCo_DamageFly_Coll,ftCo_80090184
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    seed_target = samples[case.target_record]["seed_t"]
    ref_target = samples[case.target_record]["ref_t1"]
    assert int(seed_target["action_id"][p]) == int(case.expected_seed_action), case.note
    assert int(ref_target["action_id"][p]) == int(case.expected_ref_action), case.note
    assert int(seed_target["on_ground"][p]) == 0, case.note
    assert int(ref_target["on_ground"][p]) == 1, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        out, ref, _ = _run_one_step_with_rollout(dataset_rel=case.dataset_rel, record=rec, p=p)
        assert int(out["action_id"][p]) == int(ref["action_id"][p]), f"{case.note}: record={rec}"
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]), f"{case.note}: record={rec}"
        assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4, (
            f"{case.note}: record={rec}"
        )

    # Explicit negative control: the pre-transition row stays airborne and must not snap early.
    out_prev, ref_prev, _ = _run_one_step_with_rollout(
        dataset_rel=case.dataset_rel, record=case.target_record - 1, p=p
    )
    assert int(ref_prev["on_ground"][p]) == 0, case.note
    assert int(out_prev["on_ground"][p]) == 0, case.note
