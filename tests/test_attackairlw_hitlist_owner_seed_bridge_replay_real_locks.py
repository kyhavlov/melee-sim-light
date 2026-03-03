from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_attackairlw_hitlist_owner_seed_bridge_target_pm1_both_players_strict_lock() -> None:
    # Replay-real target+/-1 lock for the AttackAirLw stale-owner hitlist bridge lane.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # - data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    target_record = 7512
    rows = (target_record - 1, target_record, target_record + 1)
    p_target = 0

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == 24  # Dash
    assert int(target["seed_t"]["hitlag"][p_target]) == 0
    assert int(target["seed_t"]["hitstun"][p_target]) == 0
    assert int(target["ref_t1"]["action_id"][p_target]) == 86  # DamageFlyHi
    assert int(target["ref_t1"]["hitlag"][p_target]) > 0
    assert int(target["ref_t1"]["hitstun"][p_target]) > 0
    # Attacker context row: AttackAirLw owner on the opposing player.
    assert int(target["seed_t"]["action_id"][1]) == 69  # AttackAirLw

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
