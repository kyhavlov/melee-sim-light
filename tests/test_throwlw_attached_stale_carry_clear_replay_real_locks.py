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
class _ThrowLwCarryCase:
    target_record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowLwCarryCase(target_record=438, note="QGD ThrowLw attached stale-carry family A"),
        _ThrowLwCarryCase(target_record=4089, note="QGD ThrowLw attached stale-carry family B"),
        _ThrowLwCarryCase(target_record=8106, note="QGD ThrowLw attached stale-carry family C"),
    ],
)
def test_throwlw_attached_stale_carry_target_pm1_both_players_strict_lock(case: _ThrowLwCarryCase) -> None:
    # Replay-real target+/-1 lock for the ThrowLw post-collision stale-carry clear lane.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
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
    ref = target["ref_t1"]

    # ThrowLw attached context preconditions from src/items.c:
    # - thrower on first ThrowLw projectile pulse crossing window (cmd1 active + crossed pulse=first)
    # - victim still attached in ThrownLw with matching owner
    # - carried item slot 1 absent in seed/ref at t and t+1
    thrower = 0
    victim = 1
    assert int(seed["action_id"][thrower]) == 222, case.note  # ThrowLw
    assert int(seed["action_id"][victim]) == 242, case.note  # ThrownLw
    assert int(seed["grab_owner_port"][victim]) == thrower, case.note
    assert abs(float(seed["anim_frame_f32"][thrower]) - 22.666667938232422) <= 1e-6, case.note
    assert int(seed["hitlag"][victim]) == 0 and int(seed["hitstun"][victim]) == 0, case.note
    assert int(seed["items"][1]["exists"]) == 0, case.note
    assert int(ref["items"][1]["exists"]) == 0, case.note

    # Strict replay-real lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, thrower)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
