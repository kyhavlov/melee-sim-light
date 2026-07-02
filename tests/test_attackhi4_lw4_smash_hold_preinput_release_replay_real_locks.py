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

MSL_BUTTON_A = 0x0100


@dataclass(frozen=True)
class _SmashHoldReleaseCase:
    dataset_rel: str
    target_record: int
    p_target: int
    expected_ref_action_frame: int
    expected_cur_a: int
    expected_prev_a: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SmashHoldReleaseCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=3753,
            p_target=0,
            expected_ref_action_frame=2,
            expected_cur_a=1,
            expected_prev_a=1,
            note="AttackHi4 af=2 held row (GAT)",
        ),
        _SmashHoldReleaseCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=4544,
            p_target=1,
            expected_ref_action_frame=2,
            expected_cur_a=0,
            expected_prev_a=1,
            note="AttackLw4 af=2 release-edge hold row (AGG)",
        ),
        _SmashHoldReleaseCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=6819,
            p_target=1,
            expected_ref_action_frame=3,
            expected_cur_a=0,
            expected_prev_a=0,
            note="AttackLw4 af=2 release row (AGG)",
        ),
        _SmashHoldReleaseCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=2804,
            p_target=1,
            expected_ref_action_frame=3,
            expected_cur_a=0,
            expected_prev_a=0,
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
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    p_target = int(case.p_target)

    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    prev_input = target["prev_input_t"]["p"][p_target]
    cur_input = target["input_t"]["p"][p_target]

    assert int(seed["action_id"][p_target]) in (63, 64), case.note
    assert int(seed["action_frame"][p_target]) == 2, case.note
    assert int(ref["action_frame"][p_target]) == int(case.expected_ref_action_frame), case.note
    assert int((int(prev_input["buttons"]) & MSL_BUTTON_A) != 0) == int(case.expected_prev_a), case.note
    assert int((int(cur_input["buttons"]) & MSL_BUTTON_A) != 0) == int(case.expected_cur_a), case.note

    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p_target)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
