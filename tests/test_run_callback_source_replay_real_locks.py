from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _RunCallbackSourceCase:
    dataset_rel: str
    target_record: int
    runner_port: int
    seed_action_frame: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _RunCallbackSourceCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            target_record=639,
            runner_port=0,
            seed_action_frame=10,
            note="GAT steady Run callback-source family",
        ),
        _RunCallbackSourceCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.slpz"
            ),
            target_record=4801,
            runner_port=1,
            seed_action_frame=3,
            note="MAG steady Run low-rate callback-source family",
        ),
    ],
)
def test_run_callback_source_rate_target_pm1_both_players_strict_lock(
    case: _RunCallbackSourceCase,
) -> None:
    # Replay-real strict lock for Run callback-owned source-rate modeling in src/anim_timebase.c.
    #
    # Decomp/data refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_Anim
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    runner = int(case.runner_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]

    assert int(seed["action_id"][runner]) == 21, case.note  # Run
    assert int(seed["action_frame"][runner]) == case.seed_action_frame, case.note
    assert int(seed["animation_index"][runner]) == 13, case.note
    assert int(seed["on_ground"][runner]) == 1, case.note
    assert int(seed["hitlag"][runner]) == 0, case.note
    assert int(seed["hitstun"][runner]) == 0, case.note
    assert abs(float(seed["run_anim_source_vel_f32"][runner])) > 0.0, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, runner)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
