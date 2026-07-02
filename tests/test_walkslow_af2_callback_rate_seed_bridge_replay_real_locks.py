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
class _WalkCallbackSourceCase:
    dataset_rel: str
    target_record: int
    walker_port: int
    seed_action_id: int
    seed_action_frame: int
    seed_animation_index: int
    note: str
    expect_retarget_source_nonzero: bool = True


_CARDINAL = "replays/validation/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/AttachedGoodNaturedGuanaco.slpz",
            target_record=1142,
            walker_port=1,
            seed_action_id=15,
            seed_action_frame=2,
            seed_animation_index=7,
            note="AGN WalkSlow af=2 callback-source family",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/AttachedGoodNaturedGuanaco.slpz",
            target_record=4391,
            walker_port=1,
            seed_action_id=16,
            seed_action_frame=2,
            seed_animation_index=8,
            note="AGN WalkMiddle af=2 callback-source family",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/QuerulousGrandDinosaur.slpz",
            target_record=3315,
            walker_port=1,
            seed_action_id=15,
            seed_action_frame=2,
            seed_animation_index=7,
            note="QGD WalkSlow af=2 callback-source family (low-rate carry)",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/QuerulousGrandDinosaur.slpz",
            target_record=3319,
            walker_port=1,
            seed_action_id=15,
            seed_action_frame=8,
            seed_animation_index=7,
            note="QGD WalkSlow mid-cycle callback-source family",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/QuerulousGrandDinosaur.slpz",
            target_record=3745,
            walker_port=1,
            seed_action_id=15,
            seed_action_frame=2,
            seed_animation_index=7,
            note="QGD WalkSlow af=2 callback-source family (neutral-state facing flip)",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=f"{_CARDINAL}/TreasuredBackKangaroo.slpz",
            target_record=672,
            walker_port=0,
            seed_action_id=15,
            seed_action_frame=7,
            seed_animation_index=7,
            note="TBK WalkSlow->WalkMiddle callback-source family",
        ),
        _WalkCallbackSourceCase(
            dataset_rel=(
                "replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.slpz"
            ),
            target_record=1190,
            walker_port=0,
            seed_action_id=16,
            seed_action_frame=33,
            seed_animation_index=8,
            note="MAG WalkMiddle->WalkFast AObj loop-wrap retarget family",
            expect_retarget_source_nonzero=False,
        ),
    ],
)
def test_walk_callback_source_rate_target_pm1_both_players_strict_lock(
    case: _WalkCallbackSourceCase,
) -> None:
    # Replay-real strict lock for shared Walk callback-owned source-rate modeling in src/anim_timebase.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::{ftCo_Walk_Enter,ftCo_Walk_Anim}
    # - refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
    # - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    walker = int(case.walker_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]

    # Lane preconditions from src/anim_timebase.c:
    # - grounded Walk steady frame;
    # - callback-owned walk source lane is populated for the same seed row;
    # - zero hitlag/hitstun to avoid unrelated pause ownership.
    assert int(seed["action_id"][walker]) == case.seed_action_id, case.note
    assert int(seed["action_frame"][walker]) == case.seed_action_frame, case.note
    assert int(seed["animation_index"][walker]) == case.seed_animation_index, case.note
    assert int(seed["on_ground"][walker]) == 1, case.note
    assert int(seed["hitlag"][walker]) == 0, case.note
    assert int(seed["hitstun"][walker]) == 0, case.note
    assert abs(float(seed["walk_anim_source_vel_f32"][walker])) > 0.0, case.note
    if int(target["ref_t1"]["action_id"][walker]) in (15, 16, 17) and int(
        target["ref_t1"]["action_id"][walker]
    ) != int(seed["action_id"][walker]):
        if case.expect_retarget_source_nonzero:
            assert abs(float(seed["walk_retarget_tick_source_vel_f32"][walker])) > 0.0, case.note
        # The general frame_speed lane remains the causal post-frame rate; the retarget source is
        # a separate hidden-owner reconstruction for ftWalkCommon_800DFDDC/800DFEC8.
        assert float(seed["frame_speed_mul_f32"][walker]) > 0.0, case.note

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
