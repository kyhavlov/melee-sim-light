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
            "AttachedGoodNaturedGuanaco.msl"
        ),
        target_record=6366,
        port=0,
        note="AGN SpecialLwStart -> FallSpecialF clears stale x2218_b5 on destination entry",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl"
        ),
        target_record=6379,
        port=1,
        note="AGN SpecialAirLwStart -> Squat clears stale x2218_b5 on destination entry",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        target_record=1223,
        port=0,
        note="QGD SpecialLwStart -> FallSpecialF clears stale x2218_b5 on destination entry",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        target_record=9643,
        port=0,
        note="GAT SpecialLwStart -> FallSpecialF clears stale x2218_b5 while preserving x2218_b2",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl"
        ),
        target_record=4031,
        port=0,
        note="TBK SpecialLwStart -> FallSpecialF clears stale x2218_b5 on destination entry",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_reflector_start_direct_exit_clears_behavior_bit(case: _Case) -> None:
    # Replay-real lock for reflector-start direct exits:
    # - startup SpecialLw states carry fp->x2218_b5 while reflector behavior is live,
    # - direct startup exits into common states do not keep a loop/hit/turn owner for that lane,
    # - so the destination entry row should clear bit0x04.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLwStart_Anim,ftFx_SpecialAirLwStart_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.port
    rows = (case.target_record - 1, case.target_record, case.target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed = target["seed_t"]
    assert (int(seed["state_flags"][p, 0]) & 0x04) != 0, case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["seed_prev_action_id"][p]) in (360, 365), case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["state_flags"][p, 0]) == int(ref_row["state_flags"][p, 0]), case.note
