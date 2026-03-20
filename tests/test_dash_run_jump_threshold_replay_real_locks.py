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
    rows: tuple[int, int, int, int]
    player: int
    target_index: int


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl"
        ),
        rows=(522, 523, 524, 525),
        player=0,
        target_index=2,
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        rows=(5162, 5163, 5164, 5165),
        player=0,
        target_index=1,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-p{c.player}")
def test_fn_800caf78_dash_jump_rows_and_controls_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for Dash/Run jump threshold parity:
    # - Dash/Run/RunBrake/TurnRun IASA call fn_800CAF78, not ftCo_Jump_GetInput.
    # - fn_800CAF78 uses p_ftCommonData->x80 for the lstick.y jump threshold.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::fn_800CAF78
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = case.rows
    p = int(case.player)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target_record = rows[case.target_index]
    target = samples[target_record : target_record + 1]
    assert int(target["seed_t"]["action_id"][0, p]) == 20  # Dash
    assert int(target["ref_t1"]["action_id"][0, p]) == 24  # KneeBend
    assert int(target["ref_t1"]["action_frame"][0, p]) == 0
    assert int(target["ref_t1"]["animation_index"][0, p]) == 15

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"record={rec} p={p} field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
        for field in ("on_ground", "jumps_left", "facing"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"record={rec} p={p} stable={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
