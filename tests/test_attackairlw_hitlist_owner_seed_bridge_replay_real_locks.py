from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_identity_lock_fields_match_ref,
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.test_post_contact_hitlag_hitlist_seed_replay_real_locks import _run_rollout_records
from tools.eval.dataset import read_dataset
from tools.eval.run_one_step_eval import COMPARE_DTYPE


def _run_reseed_rollout_records(
    ds_path: Path, start_record: int, records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(records)
    assert start_record <= min(records)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    out: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, max(records) + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out[record] = (
                    samples["ref_t1"][record].copy(),
                    out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                )
    finally:
        binding.destroy(handle)
    return out


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
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
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


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p_attacker", "p_defender", "attacker_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            3630,
            1,
            0,
            69,  # AttackAirLw
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            1206,
            0,
            1,
            69,  # AttackAirLw
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.msl",
            3276,
            0,
            1,
            67,  # AttackAirB
        ),
    ],
)
def test_attackair_replay_only_shield_admission_valid_empty_hitcapsule_locks(
    dataset_rel: str, record: int, p_attacker: int, p_defender: int, attacker_action: int
) -> None:
    # Replay-real locks for the non-causal per-HitCapsule shield-admission seed lane:
    # - The dense group seed carries an indefinite victim latch that would suppress the live
    #   shield hit.
    # - `combat_hitlist_hb_valid` marks the active HitCapsules authoritative-empty for the
    #   teacher-forced seed frame, so runtime uses the decomp-shaped HitCapsule lane instead of
    #   the coarse group fallback.
    # - This is a seed provenance lane, not a runtime row bridge; normal rollouts carry the
    #   HitCapsule rings directly.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAir*.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = (record - 1, record, record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[record]
    seed_t = target["seed_t"]
    ref_t1 = target["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == int(attacker_action)
    assert int(seed_t["hitlag"][p_attacker]) == 0
    assert int(seed_t["hitlag"][p_defender]) == 0
    assert int(ref_t1["action_id"][p_defender]) == 181  # GuardSetOff
    assert int(ref_t1["hitlag"][p_attacker]) > 0
    assert int(ref_t1["hitlag"][p_defender]) > 0
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) in (0, 0xFFFF)
    assert any(int(v) != 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    for hb in range(4):
        if int(seed_t["combat_hitlist_hb_valid"][p_attacker, hb]) != 0:
            assert all(int(x) == 0 for x in seed_t["combat_hitlist_hb_cd"][p_attacker, hb, :2])

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_defender)
        for p in (p_attacker, p_defender):
            _assert_transition_identity_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
def test_attackairlw_guard_shield_admission_negative_neighbor_stays_suppressed() -> None:
    # Negative adjacent lock for GAT:3629:
    # - This is the same AttackAirLw/Guard neighborhood as GAT:3630, but the replay does not enter
    #   GuardSetOff and the dense hitlist latch must remain suppressing.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3629
    p_attacker = 1
    p_defender = 0
    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == 69  # AttackAirLw
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) == 0xFFFF
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    assert int(ref_t1["action_id"][p_defender]) == 178  # GuardOn stays held
    assert int(ref_t1["hitlag"][p_defender]) == 0
    assert int(ref_t1["hitlag"][p_attacker]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    for p in (p_attacker, p_defender):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )


@pytest.mark.integration
def test_attackairb_guard_shield_admission_negative_neighbor_stays_suppressed() -> None:
    # Negative adjacent lock for HVG:3275:
    # - AttackAirB carries the same stale dense hitlist shape as HVG:3276, but the replay-visible
    #   next row does not enter GuardSetOff/hitlag.
    # - The replay-only authoritative-empty per-HitCapsule lane must therefore stay off.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "HilariousVillainousGiraffe.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 3275
    p_attacker = 0
    p_defender = 1
    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    ref_t1 = ds.samples[record]["ref_t1"]
    assert int(seed_t["action_id"][p_attacker]) == 67  # AttackAirB
    assert int(seed_t["combat_hitlist_cd"][p_attacker, 0, p_defender]) == 0xFFFF
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][p_attacker])
    assert int(ref_t1["action_id"][p_defender]) == 179  # Guard stays held
    assert int(ref_t1["hitlag"][p_defender]) == 0
    assert int(ref_t1["hitlag"][p_attacker]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p_defender)
    for p in (p_attacker, p_defender):
        _assert_transition_identity_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=record,
            p=p,
        )


@pytest.mark.integration
def test_attackairlw_invincible_contact_hitlag_rollout_preserves_victim_latch() -> None:
    # Replay-real rollout lock for PRH:4858:
    # - p0 AttackAirLw is reseeded inside attacker-only hitlag after no-damage contact with an
    #   invincible p1 hurtbox.
    # - The seed has no authoritative per-HitCapsule victims_1 lane; runtime must recover the
    #   decomp latch from hitlag-frozen previous capsules instead of replaying create-frame clear.
    # - Without that latch, the same Down Air hits p1's newly held shield at rec=4864 and delays
    #   p0's LandingAirLw transition.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 4858
    ds = read_dataset(str(dataset_path))
    seed_t = ds.samples[record]["seed_t"]
    assert int(seed_t["action_id"][0]) == 69  # AttackAirLw
    assert int(seed_t["hitlag"][0]) > 0
    assert int(seed_t["hurtbox_state"][1]) == 1
    assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][0])

    rows = _run_rollout_records(dataset_path, start_record=record, records=(4864, 4867))
    for rec, (ref_row, out_row) in rows.items():
        for p in (0, 1):
            for field in ("action_id", "action_frame", "hitlag", "hitstun", "shield_hp"):
                assert out_row[field][p] == ref_row[field][p], f"record={rec} p={p} field={field}"
        assert out_row["pos_y"][0] == pytest.approx(ref_row["pos_y"][0], abs=1e-5)


@pytest.mark.integration
def test_attackairlw_no_damage_contact_respects_authoritative_empty_hb_seed() -> None:
    # `combat_hitlist_hb_valid=1, combat_hitlist_hb_cd=0` is an authoritative empty HitCapsule
    # victims_1 seed. The no-damage contact materializer must not treat it as missing provenance
    # and fill the live per-HitCapsule rings from the dense fallback.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/types.h::HitCapsule
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 4858
    row = ds.samples[record : record + 1].copy()
    seed = row["seed_t"]
    attacker = 0
    victim = 1
    assert int(seed["action_id"][0, attacker]) == 69  # AttackAirLw
    assert int(seed["hitlag"][0, attacker]) > 0
    assert int(seed["hurtbox_state"][0, victim]) == 1
    seed["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(1)
    seed["combat_hitlist_hb_cd"][0, attacker, :, victim] = np.uint16(0)
    seed["combat_hitlist_hb_victim_iid"][0, attacker, :, victim] = np.uint16(0)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        assert [
            int(binding.debug_hitlist_fighter_contains(handle, 0, attacker, hb_id, victim))
            for hb_id in range(4)
        ] == [0, 0, 0, 0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attackairlw_multihit_runtime_clear_create_allows_later_dair_rehit_182447() -> None:
    # Runtime rollout lock for the AttackAirLw multi-hit clear/create owner:
    # - Fox DAir first hits at 182447:2009, then hitlag freezes the attacker through the first
    #   active band.
    # - The generated script clears on frame 10 and creates a new same-group DAir band on frame 11.
    #   ftAction_8007121C -> ftColl_800768A0 owns the empty HitCapsule victims_1 list after that
    #   clear/create, so same-source BODY attribution from the prior DAir hit must not lazily
    #   suppress the valid rec2014 re-hit.
    # - Rows 2010/2011 are the negative boundary: before the clear/create owner, the prior
    #   HitCapsule latch still suppresses another hit while the first band is active/frozen.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_CopyHitCapsule}
    # data/scripts/fox.bin (MSLFTSC1 ftCo_SM_AttackAirLw clear/create phases)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/Game_20260515T182447.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _run_reseed_rollout_records(dataset_path, start_record=2008, records=(2010, 2011, 2014))

    for rec in (2010, 2011):
        ref_row, out_row = rows[rec]
        assert int(ref_row["action_id"][0]) == int(out_row["action_id"][0]) == 76
        assert int(ref_row["hitlag"][0]) == int(out_row["hitlag"][0])
        assert float(out_row["percent"][0]) == pytest.approx(float(ref_row["percent"][0]), abs=1e-6)

    ref_row, out_row = rows[2014]
    assert int(ref_row["action_id"][0]) == int(out_row["action_id"][0]) == 82
    assert int(ref_row["hitlag"][0]) == int(out_row["hitlag"][0]) == 3
    assert int(ref_row["hitstun"][0]) == int(out_row["hitstun"][0]) == 17
    assert float(out_row["percent"][0]) == pytest.approx(float(ref_row["percent"][0]), abs=1e-6)
