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
class _BlasterGunDamageExitCase:
    dataset_rel: str
    target_record: int
    owner_port: int
    note: str


@dataclass(frozen=True)
class _BlasterGunLifetimeCase:
    dataset_rel: str
    record: int
    owner_port: int
    expect_gun: bool
    note: str


def _count_owner_gun_items(row, owner_port: int) -> int:
    count = 0
    for item in row["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) != owner_port:
            continue
        if int(item["type"]) not in (74, 75):
            continue
        count += 1
    return count


def _owner_gun_item_keys(row, owner_port: int) -> list[tuple[int, int, int]]:
    keys: list[tuple[int, int, int]] = []
    for item in row["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) != owner_port:
            continue
        if int(item["type"]) not in (74, 75):
            continue
        keys.append((int(item["type"]), int(item["state"]), int(item["instance_id"])))
    return keys


def _is_damage_action(action_id: int) -> bool:
    return action_id in {
        0x26,  # DamageFall
        0x4B,
        0x4C,
        0x4D,
        0x4E,
        0x4F,
        0x50,
        0x51,
        0x52,
        0x53,
        0x54,
        0x55,
        0x56,
        0x57,
        0x58,
        0x59,
        0x5A,
        0x5B,
    }


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=666,
            owner_port=1,
            expect_gun=False,
            note="BHH SpecialNEnd->Dash clears the stale Fox blaster gun article.",
        ),
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=8222,
            owner_port=1,
            expect_gun=False,
            note="PRH SpecialNEnd->Wait clears the stale Falco blaster gun article.",
        ),
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=2460,
            owner_port=1,
            expect_gun=True,
            note="PRH active SpecialAirNLoop keeps the Falco blaster gun article.",
        ),
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=140,
            owner_port=0,
            expect_gun=True,
            note="IAT active SpecialAirNLoop gun remains while the other stale end gun clears.",
        ),
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PriceyPartialAlbatross.msl"
            ),
            record=1184,
            owner_port=0,
            expect_gun=True,
            note="PPA adjacent SpecialAirNEnd fall keeps the gun before DeadDown.",
        ),
        _BlasterGunLifetimeCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PriceyPartialAlbatross.msl"
            ),
            record=1185,
            owner_port=0,
            expect_gun=False,
            note="PPA SpecialAirNEnd->DeadDown clears the stale blaster gun only.",
        ),
    ],
    ids=lambda c: c.note.split()[0].lower() + "_" + ("keep" if c.expect_gun else "clear"),
)
def test_blaster_gun_lifetime_replay_real_item_rows(case: _BlasterGunLifetimeCase) -> None:
    # Replay-real locks for src/items.c blaster gun lifetime/identity:
    # - ftFx_SpecialNEnd_Anim clears fp->fv.fx.x222C_blasterGObj before leaving SpecialNEnd.
    # - itFoxblaster_UnkMotion8_Anim then clears the item when
    #   ftFx_SpecialN_CheckRemoveBlaster observes the NULL fighter pointer or when
    #   ftFx_SpecialN_GetBlasterAction reports a non-blaster Dead* state.
    # - Active Start/Loop gun rows must remain attached.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNEnd_Anim,ftFx_SpecialN_CheckRemoveBlaster,ftFx_SpecialN_GetBlasterAction}
    # refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
    # refs/melee/src/melee/ft/ftmotionstates.c
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, int(case.record), int(case.owner_port))
    owner = int(case.owner_port)

    assert _count_owner_gun_items(seed, owner) >= 1, case.note
    if case.expect_gun:
        assert _count_owner_gun_items(ref, owner) >= 1, case.note
        assert _owner_gun_item_keys(out, owner) == _owner_gun_item_keys(ref, owner), case.note
    else:
        assert _count_owner_gun_items(ref, owner) == 0, case.note
        assert _count_owner_gun_items(out, owner) == 0, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2132,
            owner_port=1,
            note="GAT blaster gun stale carry on SpecialAirNLoop->DamageFlyTop transition",
        ),
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=9611,
            owner_port=1,
            note="GAT blaster gun stale carry on SpecialAirNStart->DamageAir2 transition",
        ),
        _BlasterGunDamageExitCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=4412,
            owner_port=1,
            note="TBK blaster gun stale carry on SpecialAirNLoop->DamageAir2 transition",
        ),
    ],
)
def test_blaster_gun_damage_exit_target_pm1_both_players_strict_lock(
    case: _BlasterGunDamageExitCase,
) -> None:
    # Replay-real target+/-1 strict lock for post-combat blaster-gun stale-carry clear in
    # src/items.c::items_update_post_combat.
    #
    # Decomp refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
    # - refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    owner = int(case.owner_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # Lane preconditions:
    # - seed row still has owner-attached blaster gun item;
    # - ref row has no owner blaster gun item at t+1;
    # - owner transitions into a Damage* action on t+1.
    assert _count_owner_gun_items(seed, owner) >= 1, case.note
    assert _count_owner_gun_items(ref, owner) == 0, case.note
    assert _is_damage_action(int(ref["action_id"][owner])), case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
def test_same_frame_specialairn_gun_spawn_clears_when_damage_interrupts_pte_1019() -> None:
    # PTE:1019 covers the source order missing from the older frame-start SpecialN damage-exit
    # locks: JumpF input enters SpecialAirNStart and ftFx_SpecialN_Enter creates the attached
    # blaster gun during the fighter Anim phase, then the live Falco laser hits Fox in the same
    # combat pass. itFoxblaster_UnkMotion8_Anim sees ftFx_SpecialN_GetBlasterAction==9 after
    # Fighter_ProcessHit and clears only that same-frame gun article.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialN_Enter,ftFx_SpecialN_GetBlasterAction}
    # refs/melee/src/melee/it/items/itfoxblaster.c::{it_802AE8A8,itFoxblaster_UnkMotion8_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 1019
    owner = 0
    seed, ref, out = _run_one_step_row(dataset_path, record, owner)

    assert int(seed["action_id"][owner]) == 25  # JumpF before the B-special input callback.
    assert int(seed["seed_prev_action_id"][owner]) == 25
    assert int(ref["action_id"][owner]) == 84  # DamageAir1 after the same-frame laser hit.
    assert _count_owner_gun_items(seed, owner) == 0
    assert _count_owner_gun_items(ref, owner) == 0
    assert _count_owner_gun_items(out, owner) == 0
    _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=record, p=owner)


@pytest.mark.integration
def test_same_frame_damage_clear_requires_specialn_gun_spawn_source_pte_1019() -> None:
    # Same replay row, but clear the B button that owns SpecialAirNStart entry. The Falco laser
    # still has no authority to fabricate/clear a Fox gun article because no ftFx_SpecialN_Enter
    # article episode ran this step.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def no_b_button(_prev_input_t, input_t) -> None:
        input_t["p"]["buttons"][0, 0] = 0

    _, _, out = _run_one_step_row(dataset_path, 1019, 0, input_mutator=no_b_button)
    assert _count_owner_gun_items(out, 0) == 0
