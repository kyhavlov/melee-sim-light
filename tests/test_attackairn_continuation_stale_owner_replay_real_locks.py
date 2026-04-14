from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_identity_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_attackairn_continuation_stale_owner_rows_and_adjacent_controls_are_replay_exact() -> None:
    # Replay-real lock for the kept AttackAirN continuation stale-owner subset:
    # - AGN:5482 is a later create_hitbox refresh on AttackAirN while the victim is still in
    #   DamageFlyTop hitstun from an older same-port attacker instance.
    # - The live BODY hit should land on that refresh edge and rewrite BODY attribution through
    #   ftColl_80076ED8 / Fighter_ProcessHit_8006D1EC.
    # - Keep the current queue-adjacent controls explicit: GAT:2520 is already replay-exact and
    #   AGN:7047/7048 remain the separate AttackAirLw continuation family.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    agn_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.msl"
    )
    gat_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "GracefulAttachedTurtle.msl"
    )
    if not agn_path.exists():
        pytest.skip(f"missing local dataset: {agn_path}")
    if not gat_path.exists():
        pytest.skip(f"missing local dataset: {gat_path}")

    agn = read_dataset(str(agn_path)).samples
    gat = read_dataset(str(gat_path)).samples

    for rec in (5481, 5482, 5483, 7047, 7048):
        assert int(agn.shape[0]) > rec, f"dataset too short for AGN rec={rec}"
    assert int(gat.shape[0]) > 2520, "dataset too short for GAT rec=2520"

    target_seed = agn[5482]["seed_t"]
    target_ref = agn[5482]["ref_t1"]
    p_attacker = 0
    p_victim = 1
    assert int(target_seed["action_id"][p_attacker]) == 65  # AttackAirN
    assert int(target_seed["action_frame"][p_attacker]) >= 8
    assert int(target_seed["action_id"][p_victim]) == 90  # DamageFlyTop continuation victim
    assert int(target_seed["hitstun"][p_victim]) > 0
    assert int(target_seed["instance_hit_by"][p_victim]) != int(target_seed["instance_id"][p_attacker])
    assert int(target_ref["action_id"][p_victim]) == 88  # DamageFlyN re-entry
    assert int(target_ref["hitlag"][p_victim]) > 0

    for rec in (5481, 5482, 5483, 7047, 7048):
        _, ref, out = _run_one_step_row(agn_path, rec, p_attacker)
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out,
                ref_row=ref,
                record=rec,
                p=p,
            )

    _, ref_2520, out_2520 = _run_one_step_row(gat_path, 2520, 0)
    for p in (0, 1):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_2520,
            ref_row=ref_2520,
            record=2520,
            p=p,
        )
