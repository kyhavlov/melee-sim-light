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
class _ThrowHiPulseBridgeCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    expect_last_attack_landed_nonzero: bool
    expect_thrower_facing_nonzero: bool | None
    note: str


@dataclass(frozen=True)
class _ThrowHiCrossedPrevCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    expected_crossed_prev_frame: int
    expect_matches_ref: bool
    note: str


@dataclass(frozen=True)
class _ThrowHiFrame24CarryCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    positive: bool
    expect_matches_ref: bool
    expected_victim_hitlag_min: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=583,
            thrower_port=0,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family AGG",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=984,
            thrower_port=1,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family GAT",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3091,
            thrower_port=0,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family QGD",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2137,
            thrower_port=1,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family TBK",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3424,
            thrower_port=1,
            expect_last_attack_landed_nonzero=False,
            expect_thrower_facing_nonzero=False,
            note="ThrowHi broadened ongoing-hitstun bridge (no damage provenance, left-facing)",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=5087,
            thrower_port=1,
            expect_last_attack_landed_nonzero=False,
            expect_thrower_facing_nonzero=True,
            note="ThrowHi broadened ongoing-hitstun bridge (no damage provenance, right-facing)",
        ),
    ],
)
def test_throwhi_pulse_seed_bridge_target_pm1_both_players_strict_lock(case: _ThrowHiPulseBridgeCase) -> None:
    # Replay-real target+/-1 strict lock for ThrowHi throw-pulse seed bridge lane in src/items.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # - refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Phys}
    # - data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    thrower = int(case.thrower_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]

    # ThrowHi seed-bridge preconditions from src/items.c:
    # - thrower in ThrowHi with seeded throw_pulse_consumed latch;
    # - victim in ongoing hitstun from this thrower (with or without damage provenance);
    # - seeded owner shot (state1) exists for velocity-direction bridge.
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_consumed"][thrower]) == 1, case.note
    assert int(seed["hitlag"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note
    if case.expect_last_attack_landed_nonzero:
        assert int(seed["last_attack_landed"][victim]) != 0, case.note
    else:
        assert int(seed["last_attack_landed"][victim]) == 0, case.note
    if case.expect_thrower_facing_nonzero is not None:
        assert int(seed["facing"][thrower]) == int(case.expect_thrower_facing_nonzero), case.note

    owner_state1_count = 0
    for item in seed["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) != thrower:
            continue
        if int(item["state"]) != 1:
            continue
        owner_state1_count += 1
    assert owner_state1_count >= 1, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, thrower)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=938,
            thrower_port=1,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn BHH owner p1",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1673,
            thrower_port=0,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn BHH owner p0",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4337,
            thrower_port=0,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn after prior despawn",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=937,
            thrower_port=1,
            item_slot=1,
            expected_crossed_prev_frame=18,
            expect_matches_ref=False,
            note="ThrowHi crossed-prev negative: existing state1 article is not respawn-owned",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1208,
            thrower_port=0,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=False,
            note="ThrowHi crossed-prev negative: owner already has state1 throw shot",
        ),
    ],
)
def test_throwhi_crossed_prev_frame20_article_spawn_locks(case: _ThrowHiCrossedPrevCase) -> None:
    # Replay-real locks for the narrow ThrowHi frame-20 crossed-prev article reconstruction:
    # - ftAction_80071974 sets throw_flags_b0 from set_throw_spawn_projectile.
    # - ftAction_80073354 owns command timer/cursor advancement.
    # - ftFx_Throw_Anim consumes throw_flags_b0 and spawns the state1 throw-side laser.
    # - data/moves/{fox,falco}.json has ThrowHi set_throw_spawn_projectile events at 18/20/24.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == int(
        case.expected_crossed_prev_frame
    ), case.note

    owner_state1_count = 0
    for item in seed["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) == thrower and int(item["state"]) == 1:
            owner_state1_count += 1
    if case.expect_matches_ref:
        assert owner_state1_count == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    fields = ("item_exists", "item_type", "item_state", "item_owner", "item_instance_id")
    slot = int(case.item_slot)
    for field in fields:
        item_field = field.removeprefix("item_")
        got = int(out_row["items"][slot][item_field])
        exp = int(ref_row["items"][slot][item_field])
        if case.expect_matches_ref:
            assert got == exp, f"{case.note}: {field} expected={exp} got={got}"
        else:
            assert got != exp, f"{case.note}: negative unexpectedly matched {field}={got}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3426,
            thrower_port=1,
            positive=True,
            expect_matches_ref=True,
            expected_victim_hitlag_min=3,
            note="Falco ThrowHi frame-24 current-pulse carried BODY row",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=5090,
            thrower_port=1,
            positive=True,
            expect_matches_ref=True,
            expected_victim_hitlag_min=3,
            note="Falco ThrowHi frame-24 crossed-prev carried BODY row",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3427,
            thrower_port=1,
            positive=False,
            expect_matches_ref=True,
            expected_victim_hitlag_min=0,
            note="Falco frame-24 lower-hitlag handoff consumes normally",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=937,
            thrower_port=1,
            positive=False,
            expect_matches_ref=False,
            expected_victim_hitlag_min=0,
            note="Fox frame-18 negative stays outside Falco frame-24 carry",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1208,
            thrower_port=0,
            positive=False,
            expect_matches_ref=False,
            expected_victim_hitlag_min=0,
            note="Fox frame-20 negative stays outside Falco frame-24 carry",
        ),
    ],
)
def test_falco_throwhi_frame24_carried_body_locks(case: _ThrowHiFrame24CarryCase) -> None:
    # Replay-real locks for the narrow Falco ThrowHi frame-24 carried BODY lane:
    # - ftAction_80071974 sets throw_flags_b0 from set_throw_spawn_projectile.
    # - ftFx_Throw_Anim consumes the pulse and spawns the state1 throw-side laser.
    # - The retained runtime branch is Falco-only, frame-24-only, and requires the victim to be
    #   in same-owner hitlag with cleared replay damage provenance.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # data/moves/falco.json moves["ftCo_SM_ThrowHi"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    if case.positive:
        assert int(seed["char_id"][thrower]) == 22, case.note  # Falco
        assert int(seed["hitlag"][victim]) >= int(case.expected_victim_hitlag_min), case.note
        assert int(seed["last_hit_by"][victim]) == thrower, case.note
        assert int(seed["last_attack_landed"][victim]) == 0, case.note
    else:
        assert (
            int(seed["char_id"][thrower]) != 22
            or int(seed["hitlag"][victim]) < 3
            or int(seed["throw_pulse_crossed_prev_frame"][thrower]) != 24
        )

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    if case.expect_matches_ref:
        for p in (0, 1):
            for field in ("hitlag", "hitstun", "combo_count"):
                got = int(out_row[field][p])
                exp = int(ref_row[field][p])
                assert got == exp, f"{case.note}: p={p} {field} expected={exp} got={got}"
        ref_live = sum(1 for item in ref_row["items"] if int(item["exists"]) != 0)
        out_live = sum(1 for item in out_row["items"] if int(item["exists"]) != 0)
        assert out_live == ref_live, f"{case.note}: live item count expected={ref_live} got={out_live}"
    else:
        ref_live = sum(1 for item in ref_row["items"] if int(item["exists"]) != 0)
        out_live = sum(1 for item in out_row["items"] if int(item["exists"]) != 0)
        assert out_live != ref_live, f"{case.note}: negative unexpectedly matched live item count"
