from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _ThrowHiPulseCrossingHoldJointCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiPulseCrossingHoldJointCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=1003,
            thrower_port=1,
            note="ThrowHi crossing hold-joint vector family AGG",
        ),
        _ThrowHiPulseCrossingHoldJointCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=237,
            thrower_port=0,
            note="ThrowHi crossing hold-joint vector family GAT",
        ),
        _ThrowHiPulseCrossingHoldJointCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=3094,
            thrower_port=0,
            note="ThrowHi crossing hold-joint vector family QGD",
        ),
        _ThrowHiPulseCrossingHoldJointCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            target_record=458,
            thrower_port=0,
            note="ThrowHi crossing hold-joint vector family TBK",
        ),
    ],
)
def test_throwhi_pulse_crossing_hold_joint_target_pm1_both_players_strict_lock(
    case: _ThrowHiPulseCrossingHoldJointCase,
) -> None:
    # Replay-real target+/-1 strict lock for ThrowHi pulse-crossing hold-joint vector lane in src/items.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # - refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    # - refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # - data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    thrower = int(case.thrower_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    victim = 1 - thrower

    # src/items.c ThrowHi pulse-crossing hold-joint preconditions:
    # - thrower in ThrowHi;
    # - pulse latch is reconstructed from frame-crossing (throw_pulse_consumed==0);
    # - prior-step crossing lane indicates mid/late throw pulse timing (>=20);
    # - seeded owner state1 shot exists as the already-emitted command ordinal.
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_consumed"][thrower]) == 0, case.note
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) >= 20, case.note
    assert int(seed["hitlag"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
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
