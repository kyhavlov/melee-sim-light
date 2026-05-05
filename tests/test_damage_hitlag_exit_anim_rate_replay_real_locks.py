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
class _DamageHitlagExitRateCase:
    dataset_rel: str
    target_record: int
    player: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageHitlagExitRateCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=7648,
            player=0,
            note="DamageFlyN hitlag exit advances action_frame 1->2 (QGD)",
        ),
        _DamageHitlagExitRateCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=1714,
            player=0,
            note="DamageFlyN hitlag exit advances action_frame 1->2 (TBK early)",
        ),
        _DamageHitlagExitRateCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=5408,
            player=0,
            note="DamageFlyN hitlag exit advances action_frame 1->2 (TBK late)",
        ),
    ],
)
def test_damage_hitlag_exit_zero_seed_rate_restores_hidden_damage_anim_rate(
    case: _DamageHitlagExitRateCase,
) -> None:
    # Replay-real lock for the P2 hitlag-exit callback/rate owner.
    #
    # Decomp ownership:
    # - Fighter_8006A1BC decrements hitlag at proc prio 0.
    # - Fighter_8006D10C invokes the post-hitlag callback and clears x2219_b5 on exit.
    # - Fighter_8006A360 then runs the prio-1 animation tick in the same frame.
    # The hidden fp->frame_speed_mul is not zeroed by hitlag; only the replay-visible frozen seed
    # rate is zero.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360,Fighter_8006D10C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    player = int(case.player)
    assert int(samples.shape[0]) > target_record, f"dataset too short: record={target_record}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["hitlag"][player]) == 1, case.note
    assert int(ref["hitlag"][player]) == 0, case.note
    assert int(seed["action_frame"][player]) == 1, case.note
    assert int(ref["action_frame"][player]) == 2, case.note
    assert float(seed["frame_speed_mul_f32"][player]) == 0.0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, player)
    _assert_transition_lock_fields_match_ref(
        out_row=out_row,
        ref_row=ref_row,
        record=target_record,
        p=player,
    )
