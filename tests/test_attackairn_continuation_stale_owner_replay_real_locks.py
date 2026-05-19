from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_identity_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_rollout_row(ds_path: Path, *, start_record: int, target_record: int) -> np.void:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > target_record, f"dataset too short for rollout target: record={target_record}"
    assert start_record <= target_record

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, target_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


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

    # Rollout-real boundary for the same owner: from an earlier seed, AGN reaches the lateral NAir
    # limb contact one frame before vanilla should allow the hit. The hidden limb HitCapsule carry
    # suppresses that first contact.
    out_roll_early = _run_rollout_row(agn_path, start_record=5295, target_record=5481)
    ref_roll_early = agn[5481]["ref_t1"]
    for p in (0, 1):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_roll_early,
            ref_row=ref_roll_early,
            record=5481,
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


@pytest.mark.integration
def test_attackairn_same_group_damageflytop_latch_suppresses_reseeded_rehit_on_mgs() -> None:
    # Replay-real lock for the opposite AttackAirN continuation boundary from AGN:5482:
    # - MGS:3421 reseeds into Falco AttackAirN's same-group active window while Fox is still a
    #   DamageFlyTop victim from the previous same-port Shine.
    # - Slippi has only the dense group victim lane here; no authoritative per-HitCapsule empty seed
    #   is present, so the teacher-forced seed must reconstruct vanilla's hidden HitCapsule.victims_1
    #   latch instead of clearing it on the same-group refresh.
    # - A rollout from MGS:3373 reaches the same contact at MGS:3420/3421; without the hidden latch
    #   reconstruction, the simulated late NAir hits empty air and rewrites Fox to DamageAir3.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    mgs_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "MilkyGracefulStingray.msl"
    )
    if not mgs_path.exists():
        pytest.skip(f"missing local dataset: {mgs_path}")

    mgs = read_dataset(str(mgs_path)).samples
    assert int(mgs.shape[0]) > 3421, "dataset too short for MGS lock"

    seed = mgs[3421]["seed_t"]
    ref = mgs[3421]["ref_t1"]
    assert int(seed["action_id"][0]) == 65  # AttackAirN
    assert int(seed["action_frame"][0]) >= 10
    assert int(seed["action_id"][1]) == 90  # DamageFlyTop continuation victim
    assert int(seed["hitstun"][1]) > 0
    assert int(seed["hitlag"][1]) == 0
    assert int(ref["action_id"][1]) == 90
    assert int(ref["hitlag"][1]) == 0
    assert int(ref["instance_hit_by"][1]) == 698

    _, ref_one, out_one = _run_one_step_row(mgs_path, 3421, 0)
    for p in (0, 1):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_one,
            ref_row=ref_one,
            record=3421,
            p=p,
        )

    for rec in (3420, 3421):
        out_roll = _run_rollout_row(mgs_path, start_record=3373, target_record=rec)
        ref_roll = mgs[rec]["ref_t1"]
        for p in (0, 1):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_roll,
                ref_row=ref_roll,
                record=rec,
                p=p,
            )


@pytest.mark.integration
def test_attackairn_late_window_live_airborne_victim_mgs_open_provenance_debt() -> None:
    # Open residual / package-boundary negative for FoD AttackAirN late-window dense provenance:
    # - MGS:1998 starts with only the legacy dense group lane for Falco NAir group 0; no
    #   authoritative per-HitCapsule victim lane exists.
    # - By MGS:2002 Fox has entered a live airborne JumpB state with no hitlag/hitstun and no
    #   Guard/Shield owner. Vanilla's second-create NAir HitCapsule admits the BODY hit; the dense
    #   seed from the rollout start should not continue suppressing it, but this package does not
    #   retain a replay-rollout bridge for that behavior. The durable closure is a per-HitCapsule
    #   victims_1 provenance/empty seed lane before materialization.
    # - The DamageFlyTop preservation test above remains the retained opposite boundary for
    #   same-source damage victims.
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    mgs_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "MilkyGracefulStingray.msl"
    )
    if not mgs_path.exists():
        pytest.skip(f"missing local dataset: {mgs_path}")

    mgs = read_dataset(str(mgs_path)).samples
    start = 1998
    target = 2002
    attacker = 1
    defender = 0
    start_seed = mgs[start]["seed_t"]
    target_seed = mgs[target]["seed_t"]
    target_ref = mgs[target]["ref_t1"]
    assert int(start_seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(start_seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(start_seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        start_seed["instance_id"][defender]
    )
    assert not any(int(start_seed["combat_hitlist_hb_valid"][attacker, hb]) for hb in range(4))
    assert int(target_seed["action_id"][defender]) == 26  # JumpB, not Damage/Guard.
    assert int(target_seed["hitlag"][defender]) == 0
    assert int(target_seed["hitstun"][defender]) == 0
    assert int(target_ref["action_id"][defender]) == 86  # DamageHi from the late NAir hit.

    out_roll = _run_rollout_row(mgs_path, start_record=start, target_record=target)
    assert int(out_roll["action_id"][defender]) != int(target_ref["action_id"][defender])
    assert int(out_roll["action_id"][defender]) == 26  # JumpB stays suppressed by dense lane.
    assert int(out_roll["hitlag"][defender]) == 0
