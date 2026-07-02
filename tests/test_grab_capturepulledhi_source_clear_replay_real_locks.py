from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_grab_capturepulledhi_source_clear_target_pm1_both_players_strict_lock() -> None:
    # Replay-real target+/-1 lock for Catch connect -> CapturePulledHi source clear ownership.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DA8E4}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    target_record = 5769
    rows = (target_record - 1, target_record, target_record + 1)
    p_target = 1

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == 90  # DamageFlyTop
    assert int(target["seed_t"]["last_hit_by"][p_target]) == 0
    assert int(target["ref_t1"]["action_id"][p_target]) == 223  # CapturePulledHi
    assert int(target["ref_t1"]["last_hit_by"][p_target]) == 6
    # Catch owner context on the opposing player.
    assert int(target["seed_t"]["action_id"][0]) == 212  # Catch
    assert int(target["ref_t1"]["action_id"][0]) == 213  # CatchPull

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
def test_source_clear_timer_x18c8_seed_bridge_target_pm1_both_players_strict_lock() -> None:
    # Replay-real target+/-1 lock for x18C8 source-owner clear countdown -> last_hit_by clear.
    #
    # Decomp refs:
    # - refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # - refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    # - refs/melee/src/melee/ft/types.h (fp+0x221F b3 gate; fp->dmg.x18C8)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    target_record = 868
    rows = (target_record - 1, target_record, target_record + 1)
    p_target = 0

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    # Seeded x18C8 countdown is active on this row and should clear source-owner identity at t+1.
    assert int(target["seed_t"]["source_clear_timer_x18c8"][p_target]) == 1
    assert int(target["seed_t"]["last_hit_by"][p_target]) == 1
    assert int(target["ref_t1"]["last_hit_by"][p_target]) == 6

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
