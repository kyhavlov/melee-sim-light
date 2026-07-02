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
    record: int
    p: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=6298,
        p=1,
        note="AGN AttackDash->Fall nearby negative control",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=6299,
        p=1,
        note="AGN AttackDash->Fall target-2 flank",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=6300,
        p=1,
        note="AGN AttackDash->Fall target-1 flank",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=6301,
        p=1,
        note="AGN AttackDash->Fall target row",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=6302,
        p=1,
        note="AGN AttackDash->Fall target+1 row",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4249,
        p=1,
        note="QGD AttackDash->WalkSlow nearby negative control",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4250,
        p=1,
        note="QGD AttackDash->WalkSlow target-3 flank",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4251,
        p=1,
        note="QGD AttackDash->WalkSlow target-2 flank",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4252,
        p=1,
        note="QGD AttackDash->WalkSlow target-1 flank",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4253,
        p=1,
        note="QGD AttackDash->WalkSlow target row",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        record=4254,
        p=1,
        note="QGD AttackDash->WalkSlow target+1 row",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).name}-rec{c.record}-p{c.p}")
def test_attackdash_same_facing_hold_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for the sustained same-facing AttackDash IASA subset:
    # - ftCo_AttackDash_IASA delegates to ftCo_Wait_IASA once ftCo_800D8AE0 is clear.
    # - Held same-facing main-stick input can therefore route to Walk/Dash/Fall ownership instead
    #   of remaining in AttackDash on the destination frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = int(case.record)
    p = int(case.p)
    assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if case.record == 6301:
        assert int(seed["action_id"][p]) == 50, case.note
        assert int(seed["action_frame"][p]) == 35, case.note
        assert int(seed["animation_index"][p]) == 52, case.note
        assert int(ref["action_id"][p]) == 29, case.note
        assert int(ref["action_frame"][p]) == 0, case.note
        assert int(ref["animation_index"][p]) == 20, case.note
        assert int(row["prev_input_t"][0]["p"][p]["main_x"]) == 98, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == 99, case.note

    if case.record == 4253:
        assert int(seed["action_id"][p]) == 50, case.note
        assert int(seed["action_frame"][p]) == 35, case.note
        assert int(seed["animation_index"][p]) == 52, case.note
        assert int(ref["action_id"][p]) == 15, case.note
        assert int(ref["action_frame"][p]) == 1, case.note
        assert int(ref["animation_index"][p]) == 7, case.note
        assert int(row["prev_input_t"][0]["p"][p]["main_x"]) == -90, case.note
        assert int(row["input_t"][0]["p"][p]["main_x"]) == -90, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)

    if case.record == 6301:
        for field in ("action_id", "action_frame", "animation_index", "instance_id", "on_ground"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
    elif case.record == 4253:
        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
    else:
        for field in ("action_id", "action_frame", "animation_index", "instance_id", "on_ground"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: stable field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
