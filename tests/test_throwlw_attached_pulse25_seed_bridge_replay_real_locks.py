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
class _ThrowLwPulse25Case:
    target_record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowLwPulse25Case(target_record=443, note="QGD ThrowLw attached pulse-25 bridge family A"),
        _ThrowLwPulse25Case(target_record=4094, note="QGD ThrowLw attached pulse-25 bridge family B"),
        _ThrowLwPulse25Case(target_record=8111, note="QGD ThrowLw attached pulse-25 bridge family C"),
    ],
)
def test_throwlw_attached_pulse25_seed_bridge_target_pm1_both_players_strict_lock(
    case: _ThrowLwPulse25Case,
) -> None:
    # Replay-real target+/-1 strict lock for src/items.c ThrowLw attached pulse-25 bridge lane.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    # - refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # - data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    owner_p = 0
    victim_p = 1

    # ThrowLw pulse-25 bridge preconditions from src/items.c:
    # - owner in ThrowLw with seeded prior-step pulse crossing at frame 25;
    # - victim is still attached ThrownLw to this owner.
    assert int(seed["action_id"][owner_p]) == 222, case.note  # ThrowLw
    assert int(seed["throw_pulse_crossed_prev_frame"][owner_p]) == 25, case.note
    assert int(seed["throw_pulse_consumed"][owner_p]) == 0, case.note
    assert int(seed["action_id"][victim_p]) == 242, case.note  # ThrownLw
    assert int(seed["grab_owner_port"][victim_p]) == owner_p, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner_p)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
