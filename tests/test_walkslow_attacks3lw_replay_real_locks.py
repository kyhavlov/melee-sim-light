from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


_CASES = (
    _Case(record=5604, note="AGN WalkSlow->AttackS3Lw family nearby negative control"),
    _Case(record=5605, note="AGN WalkSlow->AttackS3Lw family target-1 row"),
    _Case(record=5606, note="AGN WalkSlow->AttackS3Lw family target row"),
    _Case(record=5607, note="AGN WalkSlow->AttackS3Lw family target+1 row"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_walkslow_forward_down_a_enters_attacks3lw_agn_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for the grounded side-tilt low-angle selector:
    # - ftCo_AttackS3_CheckInput accepts forward side-tilt A input.
    # - decideAngle then routes downward stick angles into AttackS3Lw instead of neutral AttackS3S.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::{
    #   ftCo_AttackS3_CheckInput,decideAngle
    # }
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 5606:
        assert int(seed["action_id"][p]) == 15, case.note
        assert int(seed["action_frame"][p]) == 4, case.note
        assert int(seed["animation_index"][p]) == 7, case.note
        assert int(ref["action_id"][p]) == 55, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 57, case.note
        assert int(row["prev_input_t"][0]["p"][p]["buttons"]) == 0, case.note
        assert int(row["input_t"][0]["p"][p]["buttons"]) == 256, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == -73, case.note
        assert int(row["input_t"][0]["p"][p]["main_y"]) == -68, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )

    for field in ("on_ground", "facing", "jumps_left"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: stable field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )
