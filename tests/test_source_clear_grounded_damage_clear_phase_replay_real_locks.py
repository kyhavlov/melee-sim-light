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
class _GroundedClearPhaseCase:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_owner: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _GroundedClearPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=8552,
            victim_port=0,
            expect_owner=1,
            note="WalkSlow->Dash terminal grounded clear-phase bridge (QGD)",
        ),
        _GroundedClearPhaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=903,
            victim_port=0,
            expect_owner=1,
            note="WalkMiddle->Wait grounded clear-phase bridge (TBK)",
        ),
    ],
)
def test_source_clear_grounded_damage_clear_phase_target_pm1_both_players_strict_lock(
    case: _GroundedClearPhaseCase,
) -> None:
    # Replay-real target+/-1 strict lock for grounded source-clear phase bridge in src/timers.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
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

    assert int(seed["source_clear_timer_x18c8"][victim]) > 0, case.note
    assert int(seed["source_clear_owner_set_phase"][victim]) == 1, case.note
    assert int(seed["source_clear_grounded_damage_clear_phase"][victim]) == 1, case.note
    assert int(seed["last_hit_by"][victim]) == int(case.expect_owner), case.note
    assert int(ref["last_hit_by"][victim]) == 6, case.note
    assert (int(seed["state_flags"][victim, 4]) & 0x10) == 0, case.note

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
