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
class _WalkSlowAf2Case:
    dataset_rel: str
    target_record: int
    walker_port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _WalkSlowAf2Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3315,
            walker_port=1,
            note="QGD WalkSlow af=2 callback-source family (low-rate carry)",
        ),
        _WalkSlowAf2Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3745,
            walker_port=1,
            note="QGD WalkSlow af=2 callback-source family (neutral-state facing flip)",
        ),
    ],
)
def test_walkslow_af2_callback_rate_seed_bridge_target_pm1_both_players_strict_lock(
    case: _WalkSlowAf2Case,
) -> None:
    # Replay-real strict lock for WalkSlow callback-owned source-rate modeling in src/anim_timebase.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::{ftCo_Walk_Enter,ftCo_Walk_Anim}
    # - refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    walker = int(case.walker_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]

    # Lane preconditions from src/anim_timebase.c:
    # - WalkSlow steady frame (`action_frame==2`, `animation_index==7`) on ground;
    # - callback-owned walk source lane is populated for the same seed row;
    # - zero hitlag/hitstun to avoid unrelated pause ownership.
    assert int(seed["action_id"][walker]) == 15, case.note  # WalkSlow
    assert int(seed["action_frame"][walker]) == 2, case.note
    assert int(seed["animation_index"][walker]) == 7, case.note
    assert int(seed["on_ground"][walker]) == 1, case.note
    assert int(seed["hitlag"][walker]) == 0, case.note
    assert int(seed["hitstun"][walker]) == 0, case.note
    assert abs(float(seed["walk_anim_source_vel_f32"][walker])) > 0.0, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, walker)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
