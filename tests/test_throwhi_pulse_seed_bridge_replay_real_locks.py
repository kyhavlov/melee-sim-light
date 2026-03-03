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
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=583,
            thrower_port=0,
            note="ThrowHi pulse bridge family AGG",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=984,
            thrower_port=1,
            note="ThrowHi pulse bridge family GAT",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3091,
            thrower_port=0,
            note="ThrowHi pulse bridge family QGD",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2137,
            thrower_port=1,
            note="ThrowHi pulse bridge family TBK",
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
    # - victim in ongoing hitstun from this thrower with nonzero damage provenance;
    # - seeded owner shot (state1) exists for velocity-direction bridge.
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_consumed"][thrower]) == 1, case.note
    assert int(seed["hitlag"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note
    assert int(seed["last_attack_landed"][victim]) != 0, case.note

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
