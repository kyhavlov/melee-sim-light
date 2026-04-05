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
    dataset_rel: str
    target_record: int
    port: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        target_record=2520,
        port=0,
        note="GAT damage-to-damage re-entry clears stale x221C_b0 on the new DamageAir3 row",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl"
        ),
        target_record=2749,
        port=0,
        note="TBK damage-to-damage re-entry clears stale x221C_b0 on the new DamageFlyN row",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_damage_to_damage_reentry_rows_and_adjacent_controls_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for Damage* -> different Damage* transition reset:
    # - a fresh hit re-enters ftCo_8008DCE0 / ftCo_8008EC90 while already in Damage*,
    # - Fighter_ChangeMotionState clears fp->x221C_b0 on the destination entry, and
    # - the new Damage* row still carries hitlag/hitstun from the fresh hit.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (case.target_record - 1, case.target_record, case.target_record + 1)
    p = case.port
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert 75 <= int(seed["action_id"][p]) <= 91, case.note
    assert 75 <= int(ref["action_id"][p]) <= 91, case.note
    assert int(seed["action_id"][p]) != int(ref["action_id"][p]), case.note
    assert int(seed["state_flags"][p, 3]) == 130, case.note
    assert int(ref["state_flags"][p, 3]) == 2, case.note
    assert int(ref["action_frame"][p]) == 1, case.note
    assert int(ref["hitlag"][p]) > 0, case.note
    assert int(ref["hitstun"][p]) > 0, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p]), case.note
        assert int(out_row["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
        assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]), (
            f"{case.note}: record={rec} state_flags[3] expected={int(ref_row['state_flags'][p, 3])} "
            f"got={int(out_row['state_flags'][p, 3])}"
        )
