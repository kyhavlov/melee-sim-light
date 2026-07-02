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
class _EscapeNAllowInterruptCase:
    dataset_rel: str
    target_record: int
    player_port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _EscapeNAllowInterruptCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=262,
            player_port=0,
            note="EscapeN allow_interrupt state_flags family AGG",
        ),
        _EscapeNAllowInterruptCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=5960,
            player_port=1,
            note="EscapeN allow_interrupt state_flags family GAT",
        ),
        _EscapeNAllowInterruptCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=2419,
            player_port=0,
            note="EscapeN allow_interrupt state_flags family QGD",
        ),
        _EscapeNAllowInterruptCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            target_record=1092,
            player_port=0,
            note="EscapeN allow_interrupt state_flags family TBK",
        ),
    ],
)
def test_escapen_allow_interrupt_state_flags_target_pm1_both_players_strict_lock(
    case: _EscapeNAllowInterruptCase,
) -> None:
    # Replay-real target+/-1 strict lock for EscapeN allow_interrupt state_flags lane in
    # src/state_flags.c + src/move_tables.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # - data/moves/{fox,falco}.json moves["ftCo_SM_EscapeN"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    p = int(case.player_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # src/state_flags.c lane preconditions:
    # - EscapeN motion-state is action-stable and crosses into the script-owned allow_interrupt
    #   window at this row.
    assert int(seed["action_id"][p]) == 235, case.note  # EscapeN
    assert int(ref["action_id"][p]) == 235, case.note
    assert int(seed["action_frame"][p]) == 19, case.note
    assert int(ref["action_frame"][p]) == 20, case.note
    assert (int(seed["state_flags"][p][0]) & 0x80) == 0, case.note
    assert (int(ref["state_flags"][p][0]) & 0x80) != 0, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        for player in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=player,
            )
