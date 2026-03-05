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

MSL_BUTTON_A = 0x0100


@dataclass(frozen=True)
class _SmashHoldReleaseCase:
    dataset_rel: str
    target_record: int
    p_target: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SmashHoldReleaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6819,
            p_target=1,
            note="AttackLw4 af=2 release row (AGG)",
        ),
        _SmashHoldReleaseCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=2804,
            p_target=1,
            note="AttackHi4 af=2 release row (QGD)",
        ),
    ],
)
def test_attackhi4_lw4_smash_hold_preinput_release_target_pm1_both_players_strict_lock(
    case: _SmashHoldReleaseCase,
) -> None:
    # Replay-real strict lock for anim-timebase smash hold/release ownership in src/anim_timebase.c.
    #
    # Decomp/data refs:
    # - refs/melee/src/melee/ft/ftattacks4combo.c::ftCo_800CECE8
    # - refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # - refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackHi4.c,ftCo_AttackLw4.c}
    # - data/moves/{fox,falco}.json::ftCo_SM_Attack{Hi4,Lw4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    p_target = int(case.p_target)

    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    prev_input = target["prev_input_t"]["p"][p_target]
    cur_input = target["input_t"]["p"][p_target]

    assert int(seed["action_id"][p_target]) in (63, 64), case.note
    assert int(seed["action_frame"][p_target]) == 2, case.note
    assert int(ref["action_frame"][p_target]) == 3, case.note
    assert (int(prev_input["buttons"]) & MSL_BUTTON_A) == 0, case.note
    assert (int(cur_input["buttons"]) & MSL_BUTTON_A) == 0, case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )

