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
    ref_action_id: int
    ref_animation_index: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        negative_record=2279,
        target_record=2281,
        p=1,
        ref_action_id=24,
        ref_animation_index=15,
        note="QGD AttackDash jump-button family",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
        ),
        negative_record=2700,
        target_record=2702,
        p=1,
        ref_action_id=56,
        ref_animation_index=58,
        note="QGD AttackDash A-button family",
    ),
    _Case(
        dataset_rel="replays/validation/sheik/StiffLustrousZebra.slpz",
        negative_record=363,
        target_record=364,
        p=0,
        ref_action_id=214,
        ref_animation_index=243,
        note="Sheik AttackDash x2340 boost-grab R-press seed lane",
    ),
    _Case(
        dataset_rel="replays/validation/sheik/TenseSameHummingbird.slpz",
        negative_record=510,
        target_record=511,
        p=0,
        ref_action_id=214,
        ref_animation_index=243,
        note="Sheik AttackDash x2340 boost-grab Z-press seed lane",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.target_record}")
def test_attackdash_wait_iasa_button_edges_qgd_replay_real_lock(case: _Case) -> None:
    # Replay-real lock for the kept AttackDash IASA button-edge delegation:
    # - ftCo_AttackDash_IASA clears the x0 pre-gate, then calls ftCo_Wait_IASA when
    #   fp->allow_interrupt is true.
    # - Keep this lane restricted to explicit button-edge rows; analog-only movement rows remain on
    #   the narrower baseline path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = case.dataset_rel
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

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
    assert int(seed_t["action_id"][p]) == 50, case.note  # AttackDash
    assert int(ref_t1["action_id"][p]) == int(case.ref_action_id), case.note
    assert int(ref_t1["animation_index"][p]) == int(case.ref_animation_index), case.note

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
            "jumps_left",
            "facing",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.note}: record={rec} field={field} expected={int(ref_row[field][p])} "
                f"got={int(out_row[field][p])}"
            )
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], (
            f"{case.note}: record={rec} field=state_flags "
            f"expected={[int(x) for x in ref_row['state_flags'][p]]} "
            f"got={[int(x) for x in out_row['state_flags'][p]]}"
        )
