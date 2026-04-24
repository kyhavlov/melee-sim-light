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
class _DamageFlyRoll8006CDA4Case:
    dataset_rel: str
    target_record: int
    victim_port: int
    expect_seed_count: int
    expect_action_id: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2694,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackAirB carry now lands DamageFlyRoll (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=5717,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume grounded ThrownF hitlag carry now lands DamageFlyRoll (GAT)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6020,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="DamageFlyTop <- AttackAirB positive control remains DamageFlyRoll without early create-window carry (AGG)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=6929,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="double-consume DamageFlyTop <- AttackAirB carry now lands DamageFlyRoll (TBK)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=11134,
            victim_port=1,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume LandingAirLw carry now lands DamageFlyRoll (GAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=5391,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume AttackLw3 carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=1338,
            victim_port=0,
            expect_seed_count=1,
            expect_action_id=91,
            note="single-consume Fox SpecialLwEnd carry now lands DamageFlyRoll (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=2933,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=87,
            note="SpecialLwEnd entry control does not consume before the DamageFlyHi branch (FSP aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            target_record=11154,
            victim_port=0,
            expect_seed_count=2,
            expect_action_id=87,
            note="late LandingAirLw double-consume carry now lands DamageFlyHi (IAT aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
            target_record=6002,
            victim_port=1,
            expect_seed_count=2,
            expect_action_id=91,
            note="LandingAirLw entry double-consume carry now lands DamageFlyRoll (PPA aggregate)",
        ),
        _DamageFlyRoll8006CDA4Case(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4065,
            victim_port=0,
            expect_seed_count=0,
            expect_action_id=91,
            note="AttackHi4 carry is admitted without an extra Fighter_8006CDA4 consume (BHH aggregate)",
        ),
    ],
)
def test_fighter_8006cda4_pre_gate_consume_count_replay_real_locks(
    case: _DamageFlyRoll8006CDA4Case,
) -> None:
    # Replay-real seed and one-step locks for the explicit Fighter_8006CDA4 pre-gate consume-count
    # owner.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[int(case.target_record)]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    victim = int(case.victim_port)

    assert (
        int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == int(case.expect_seed_count)
    ), case.note

    if int(case.expect_seed_count) == 1:
        assert int(seed["action_id"][victim]) in {57, 67, 74, 363}, case.note
        assert int(seed["hitlag"][victim]) == 0, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 239:
        assert int(seed["action_id"][victim]) == 239, case.note  # ThrownF
        assert int(seed["hitlag"][victim]) > 0, case.note
        assert (int(seed["state_flags"][victim, 1]) & 0x10) != 0, case.note
    if int(case.expect_seed_count) == 2 and int(seed["action_id"][victim]) == 90:
        attacker = int(seed["last_hit_by"][victim])
        assert int(seed["action_id"][victim]) == 90, case.note  # DamageFlyTop
        assert int(seed["hitlag"][victim]) == 0, case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["on_ground"][victim]) == 0, case.note
        assert attacker in (0, 1), case.note
        assert int(seed["action_id"][attacker]) == 67, case.note  # AttackAirB
        assert int(seed["action_frame"][attacker]) >= 6, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, int(case.target_record), victim)
    assert int(out_row["action_id"][victim]) == int(case.expect_action_id), case.note
    if int(case.expect_action_id) == int(ref["action_id"][victim]):
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=int(case.target_record),
                p=p,
            )
