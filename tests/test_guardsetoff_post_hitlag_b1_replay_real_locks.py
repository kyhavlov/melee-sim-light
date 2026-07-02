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
    port: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz"
        ),
        target_record=4048,
        port=1,
        note="AGN first steady post-hitlag GuardSetOff row clears stale x221C_b1",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        target_record=851,
        port=0,
        note="GAT first steady post-hitlag GuardSetOff row clears stale x221C_b1",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz"
        ),
        target_record=3497,
        port=0,
        note="TBK first steady post-hitlag GuardSetOff row clears stale x221C_b1",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_guardsetoff_post_hitlag_b1_rows_and_adjacent_controls_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for the stale-x221C_b1 GuardSetOff carry lane:
    # - the preceding replay row is the frozen GuardSetOff af0 snapshot,
    # - the target row is the first steady post-hitlag GuardSetOff row, and
    # - GuardSetOff_Anim -> ftCo_80093BC0 has resumed ownership, clearing x221C_b1 when x14 is
    #   already expired while leaving x221C_b2 untouched.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (case.target_record - 1, case.target_record, case.target_record + 1)
    p = case.port
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[case.target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][p]) == 181, case.note  # GuardSetOff
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["seed_prev_action_id"][p]) == 181, case.note
    assert int(seed["seed_prev_action_frame"][p]) == 0, case.note
    assert int(seed["guard_reflect_timer_x14"][p]) == 0, case.note
    assert int(seed["state_flags"][p, 3]) == 96, case.note
    assert int(ref["state_flags"][p, 3]) == 32, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]), (
            f"{case.note}: record={rec} state_flags[3] expected={int(ref_row['state_flags'][p, 3])} "
            f"got={int(out_row['state_flags'][p, 3])}"
        )
