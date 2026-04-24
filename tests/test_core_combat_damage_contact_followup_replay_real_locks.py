from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _DamageContactCase:
    dataset_rel: str
    record: int
    port: int
    seed_action: int
    ref_action: int
    note: str
    expected_seed_hitlag: int = 0
    require_ref_hitlag_zero: bool = True


def _assert_transition_fields_match_ref(*, out_row, ref_row, record: int, p: int) -> None:
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "jumps_left",
        "on_ground",
        "hitlag",
        "hitstun",
    ):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"

    got_flags = [int(x) for x in out_row["state_flags"][p].tolist()]
    want_flags = [int(x) for x in ref_row["state_flags"][p].tolist()]
    assert got_flags == want_flags, f"record={record} p={p} field=state_flags"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            record=5656,
            port=0,
            seed_action=38,
            ref_action=29,
            note="held-stick DamageFall_IASA enters Fall from reconstructed x670",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            record=2545,
            port=1,
            seed_action=90,
            ref_action=29,
            note="DamageFly_IASA reaches DamageFall_IASA x670 Fall handoff",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            record=5255,
            port=0,
            seed_action=90,
            ref_action=29,
            note="DamageFly_IASA x670 Fall handoff from aggregate validation row",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            record=869,
            port=1,
            seed_action=90,
            ref_action=29,
            note="DamageFly_IASA x670 Fall handoff preserves same-frame ordering",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            record=383,
            port=0,
            seed_action=90,
            ref_action=29,
            note="DamageFly_IASA x670 Fall handoff source-owner lock",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            record=8041,
            port=0,
            seed_action=193,
            ref_action=29,
            note="airborne DownDamageD anim-end exits to Fall",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            record=8024,
            port=0,
            seed_action=183,
            ref_action=193,
            note="downed contact damage routes through DownDamageD override",
            require_ref_hitlag_zero=False,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            record=3562,
            port=1,
            seed_action=183,
            ref_action=193,
            note="DownBound downed contact damage routes through DownDamageD",
            require_ref_hitlag_zero=False,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            record=1510,
            port=1,
            seed_action=183,
            ref_action=193,
            note="downed contact damage routes through DownDamageD override",
            require_ref_hitlag_zero=False,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            record=5473,
            port=1,
            seed_action=191,
            ref_action=193,
            note="DownWaitD downed contact damage routes through DownDamageD",
            require_ref_hitlag_zero=False,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            record=3577,
            port=1,
            seed_action=193,
            ref_action=193,
            note="airborne DownDamage_Coll hitlag-exit floor contact uses ft_80081DD4",
            expected_seed_hitlag=1,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            record=6063,
            port=1,
            seed_action=193,
            ref_action=196,
            note="grounded DownDamageD anim-end with live hidden x0 enters DownWaitD then DownFowardD IASA",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            record=430,
            port=0,
            seed_action=183,
            ref_action=186,
            note="DownBoundU getup into DownStandU does not immediate-tick DownStand entry",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            record=2137,
            port=0,
            seed_action=191,
            ref_action=194,
            note="DownBoundD getup into DownStandD does not immediate-tick DownStand entry",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            record=4229,
            port=0,
            seed_action=80,
            ref_action=251,
            note="grounded Damage_Coll ledge-slip floor loss enters MissFoot",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            record=193,
            port=0,
            seed_action=84,
            ref_action=27,
            note="DamageAir1 IASA consumes current-frame jump into JumpAerialF",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            record=4187,
            port=0,
            seed_action=84,
            ref_action=69,
            note="DamageAir1 IASA consumes AttackAirLw before JumpAerial fallback",
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            record=902,
            port=0,
            seed_action=90,
            ref_action=203,
            note="DamageFlyTop wall-contact tech routes through PassiveWallJump",
            require_ref_hitlag_zero=False,
        ),
        _DamageContactCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            record=892,
            port=0,
            seed_action=352,
            ref_action=90,
            note="SpecialAirSEnd pre-Anim collision pose is hittable by same-frame aerial Shine Start BODY",
            require_ref_hitlag_zero=False,
        ),
    ],
)
def test_core_damage_contact_followup_rows_are_replay_exact(case: _DamageContactCase) -> None:
    # Replay-real locks for the retained core combat/contact follow-up pass:
    # - DamageFall_IASA uses x670_timer_lstick_tilt_x for the Fall handoff.
    # - DamageFly_IASA delegates into that same DamageFall_IASA terminal stick-Fall gate.
    # - DownDamage_Anim exits airborne DownDamage through Fall when x2224_b2 is clear.
    # - Grounded DownDamage_Anim enters DownWaitU/D while hidden mv.co.downdamage.x0 remains live,
    #   and the newly-entered DownWait can run IASA later in the same frame.
    # - Downed contact damage runs ftCo_8009F0F0 / ftCo_8009F184 before generic damage selection.
    # - DownStand entry through ftCo_80098160 does not do the immediate ftAnim tick used by
    #   DownWait/DownFoward/DownBack entry helpers.
    # - Damage_IASA delegates to Fall_IASA_Inner, whose order admits SpecialAir, AttackAir, then
    #   JumpAerial on common airborne Damage rows once x221C_b6 is clear.
    # - DamageFly_Coll uses the shared wall-tech callback for all DamageFly variants, not only
    #   DamageFlyN.
    # - Side-B End steady-state hurtcaps can still be consumed from the pre-Anim collision pose by
    #   same-frame BODY selection; PPA rec=892 is locked by ftColl_80076ED8 probe evidence.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
    #   ftCo_80097F38,ftCo_DownWait_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_8009F0F0,ftCo_8009F184}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_80098160
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialAirSEnd_Anim,ftFx_SpecialAirSEnd_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > case.record, case.note
    row = ds.samples[case.record]
    p = int(case.port)
    assert int(row["seed_t"]["action_id"][p]) == int(case.seed_action), case.note
    assert int(row["ref_t1"]["action_id"][p]) == int(case.ref_action), case.note
    assert int(row["seed_t"]["hitlag"][p]) == int(case.expected_seed_hitlag), case.note
    if case.require_ref_hitlag_zero:
        assert int(row["ref_t1"]["hitlag"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, p)
    _assert_transition_fields_match_ref(out_row=out_row, ref_row=ref_row, record=case.record, p=p)


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "attacker",
        "defender",
        "attacker_action",
        "ref_hitlag",
        "expect_hb_authoritative_empty",
    ),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            6479,
            1,
            0,
            0x0043,  # AttackAirB
            8,
            False,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            2780,
            1,
            0,
            0x0041,  # AttackAirN
            6,
            True,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            2217,
            0,
            1,
            0x0043,  # AttackAirB
            5,
            True,
        ),
    ],
)
def test_attackair_guard_reentry_shield_hitlist_rows_are_replay_exact(
    dataset_rel: str,
    record: int,
    attacker: int,
    defender: int,
    attacker_action: int,
    ref_hitlag: int,
    expect_hb_authoritative_empty: bool,
) -> None:
    # Shield re-entry HitCapsule provenance:
    # - legacy dense fallback rows carry only group-level combat_hitlist_cd/victim_iid,
    # - the replay-only authoritative-empty per-HitCapsule lane is used when t+1 proves the stale
    #   dense fallback would suppress a live AttackAir shield hit,
    # - the t+1 proof is GuardSetOff + both-fighter hitlag; shield HP loss is not required because
    #   ftColl_80076CBC's powershield-active x221C_b2 branch skips normal shield-damage accumulation.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076808}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = ds.samples[record]
    seed_t = row["seed_t"]
    ref_t1 = row["ref_t1"]
    assert int(seed_t["action_id"][attacker]) == int(attacker_action)
    assert int(seed_t["hitlag"][attacker]) == 0
    assert int(seed_t["hitlag"][defender]) == 0
    assert int(seed_t["hitstun"][attacker]) == 0
    assert int(seed_t["hitstun"][defender]) == 0
    assert int(seed_t["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    hb_valid = [int(x) for x in seed_t["combat_hitlist_hb_valid"][attacker].tolist()]
    hb_cd = [int(x) for x in seed_t["combat_hitlist_hb_cd"][attacker, :, defender].tolist()]
    if expect_hb_authoritative_empty:
        assert any(hb_valid)
        assert all(cd == 0 for cd in hb_cd)
    else:
        assert not any(hb_valid)
    assert int(ref_t1["action_id"][defender]) == 0x00B5  # GuardSetOff
    assert int(ref_t1["hitlag"][defender]) == int(ref_hitlag)
    assert int(ref_t1["hitlag"][attacker]) == int(ref_hitlag)

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, defender)
    for p in (0, 1):
        _assert_transition_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_ongoing_guardsetoff_hitlag_seeds_fighter_shield_hitlist_carry() -> None:
    # Ongoing fighter shield-hit carry:
    # - record 10264 proves the AttackAirN -> Guard shield hit and GuardSetOff hitlag entry,
    # - records 10265..10270 are the frozen shield-hit segment,
    # - the last-hitlag row must not accept the same aerial HitCapsule as a fresh shield hit.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target = 10270
    assert int(samples.shape[0]) > target
    onset = samples[10264]
    assert int(onset["seed_t"]["action_id"][1]) == 0x0041  # AttackAirN
    assert int(onset["ref_t1"]["action_id"][0]) == 0x00B5  # GuardSetOff
    assert int(onset["ref_t1"]["hitlag"][0]) > 0
    assert int(onset["ref_t1"]["hitlag"][1]) > 0
    assert float(onset["ref_t1"]["shield_hp"][0]) < float(onset["seed_t"]["shield_hp"][0])

    seed_t = samples[target]["seed_t"]
    ref_t1 = samples[target]["ref_t1"]
    assert int(seed_t["action_id"][0]) == 0x00B5
    assert int(seed_t["action_id"][1]) == 0x0041
    assert int(seed_t["hitlag"][0]) == 1
    assert int(seed_t["hitlag"][1]) == 1
    assert int(seed_t["guard_setoff_hitlag_damage_min"][0]) > 0
    assert int(ref_t1["hitlag"][0]) == 0
    assert int(ref_t1["hitlag"][1]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, target, 0)
    for p in (0, 1):
        _assert_transition_fields_match_ref(out_row=out_row, ref_row=ref_row, record=target, p=p)
