from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    negative_record: int
    target_record: int
    p: int
    note: str


_CASES = (
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        negative_record=784,
        target_record=786,
        p=0,
        note="AGN grounded DamageAir2 to Squat family",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        negative_record=4428,
        target_record=4430,
        p=1,
        note="TBK grounded DamageAir2 to Squat family",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}")
def test_grounded_damageair_wait_iasa_squat_target_pm1_with_negative_control(case: _Case) -> None:
    # Replay-real lock for grounded DamageAir* -> Wait_IASA -> Squat:
    # - grounded Damage_IASA delegates to Wait_IASA when x221C_b6 is clear
    # - Wait_IASA checks Squat after guard/jump and before Turn/Walk
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p
    for rec in (
        case.negative_record,
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
    ):
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed_t = target["seed_t"]
    ref_t1 = target["ref_t1"]
    assert int(seed_t["action_id"][p]) == 85, case.note  # DamageAir2
    assert int(seed_t["on_ground"][p]) == 1, case.note
    assert int(ref_t1["action_id"][p]) == 39, case.note  # Squat
    assert int(ref_t1["animation_index"][p]) == 30, case.note

    for rec in (
        case.negative_record,
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
    ):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "instance_id",
            "on_ground",
            "facing",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: record={rec} field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
        assert list(out_row["state_flags"][p]) == list(ref_row["state_flags"][p]), (
            f"{case.note}: record={rec} field=state_flags expected={list(ref_row['state_flags'][p])} "
            f"got={list(out_row['state_flags'][p])}"
        )
