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
class _DamageGroundWaitIasaCase:
    dataset_rel: str
    record: int
    player: int
    expected_action_id: int
    expected_animation_index: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=4036,
            player=1,
            expected_action_id=15,  # WalkSlow
            expected_animation_index=7,  # ftCo_SM_WalkSlow
            note="DamageHi1 grounded Wait_IASA walk branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=1948,
            player=0,
            expected_action_id=20,  # Dash
            expected_animation_index=12,  # ftCo_SM_Dash
            note="DamageN1 grounded Wait_IASA dash branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=7260,
            player=0,
            expected_action_id=39,  # Squat
            expected_animation_index=30,  # ftCo_SM_Squat
            note="DamageHi1 grounded Wait_IASA squat branch",
        ),
        _DamageGroundWaitIasaCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            record=8765,
            player=0,
            expected_action_id=18,  # Turn
            expected_animation_index=10,  # ftCo_SM_Turn
            note="DamageN2 grounded Wait_IASA turn branch",
        ),
    ],
)
def test_damage_ground_wait_iasa_replay_real_transition_locks(case: _DamageGroundWaitIasaCase) -> None:
    # Replay-real locks for grounded Damage_IASA -> Wait_IASA delegation in src/knockdown.c.
    #
    # Decomp:
    # - grounded Damage_IASA delegates to Wait_IASA when fp->x221C_b6 is clear.
    # - Wait_IASA then owns locomotion interrupts including Dash/Squat/Turn/Walk.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = int(case.player)
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]

    seed = row["seed_t"]
    ref = row["ref_t1"]

    assert int(seed["on_ground"][0, p]) == 1, case.note
    assert int(seed["hitlag"][0, p]) == 0, case.note
    assert int(seed["action_id"][0, p]) in (75, 78, 79), case.note  # DamageHi1 / DamageN1 / DamageN2
    assert int(ref["action_id"][0, p]) == case.expected_action_id, case.note
    assert int(ref["animation_index"][0, p]) == case.expected_animation_index, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)
