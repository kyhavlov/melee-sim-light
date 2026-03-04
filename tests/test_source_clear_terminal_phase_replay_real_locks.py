from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _SourceClearTerminalPhaseCase:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_owner: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2143,
            victim_port=0,
            expect_owner=1,
            note="ref 1->out 6 residual cleanup (AGN)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=1670,
            victim_port=1,
            expect_owner=0,
            note="ref 0->out 6 residual cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=1936,
            victim_port=0,
            expect_owner=1,
            note="ref 1->out 6 residual cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=10537,
            victim_port=1,
            expect_owner=0,
            note="ref 0->out 6 residual cleanup (GAT late)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=1905,
            victim_port=0,
            expect_owner=1,
            note="Wait followup terminal cleanup (GAT)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=5208,
            victim_port=0,
            expect_owner=1,
            note="EscapeF followup terminal cleanup (QGD)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6501,
            victim_port=0,
            expect_owner=1,
            note="SpecialSEnd followup terminal cleanup (AGN)",
        ),
        _SourceClearTerminalPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=8198,
            victim_port=1,
            expect_owner=0,
            note="SpecialSEnd followup terminal cleanup (QGD)",
        ),
    ],
)
def test_source_clear_terminal_phase_target_pm1_both_players_strict_lock(
    case: _SourceClearTerminalPhaseCase,
) -> None:
    # Replay-real target+/-1 strict lock for x18C8 terminal clear phase bridge in src/timers.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # - refs/melee/src/melee/ft/types.h (fp+0x221F b3 gate; dmg.x18C8 countdown)
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    victim = int(case.victim_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # Terminal x18C8 row with modeled defer-phase bridge active.
    assert int(seed["source_clear_timer_x18c8"][victim]) == 1, case.note
    assert int(seed["source_clear_terminal_phase"][victim]) == 1, case.note
    assert int(seed["last_hit_by"][victim]) == int(case.expect_owner), case.note
    assert int(ref["last_hit_by"][victim]) == int(case.expect_owner), case.note
    # x221F_b3 gate must be clear on the terminal tick for decomp timer ownership path.
    assert (int(seed["state_flags"][victim, 4]) & 0x10) == 0, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, victim)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
            got_last_hit_by = int(out_row["last_hit_by"][p])
            exp_last_hit_by = int(ref_row["last_hit_by"][p])
            assert (
                got_last_hit_by == exp_last_hit_by
            ), f"record={rec} p={p} field=last_hit_by expected={exp_last_hit_by} got={got_last_hit_by}"
