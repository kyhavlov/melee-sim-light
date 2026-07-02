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
    target_record: int
    p: int
    note: str


_CASES = (
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        target_record=999,
        p=0,
        note="AGN zero-KB laser carry clears x221C_b0 on the carry row",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        target_record=2469,
        p=0,
        note="AGN late zero-KB laser carry clears x221C_b0 on the carry row",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        target_record=414,
        p=1,
        note="GAT zero-KB laser carry clears x221C_b0 on the carry row",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        target_record=453,
        p=1,
        note="TBK zero-KB laser carry clears x221C_b0 on the carry row",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.p}")
def test_zero_kb_item_x221c_b0_rows_and_adjacent_controls_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for the zero-KB item carry lane:
    # - accepted zero-KB item hits can add percent without a fresh Damage* entry,
    # - the stale x221C_b0 carry should not persist after the accepted hit,
    # - adjacent controls must remain replay-exact.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p
    rows = (case.target_record - 1, case.target_record, case.target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["state_flags"][p, 3]) == 130, case.note
    assert int(ref["state_flags"][p, 3]) == 2, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]), (
            f"{case.note}: record={rec} expected state_flags[3]={int(ref_row['state_flags'][p, 3])} "
            f"got={int(out_row['state_flags'][p, 3])}"
        )
