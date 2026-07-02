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
class _CatchDashPull221CYResetCase:
    dataset_rel: str
    target_record: int
    owner_port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _CatchDashPull221CYResetCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=3403,
            owner_port=1,
            note="CatchDashPull x221C_u16_y clear family GAT",
        ),
    ],
)
def test_catchdashpull_state_flags_221c_y_reset_target_pm1_both_players_strict_lock(
    case: _CatchDashPull221CYResetCase,
) -> None:
    # Replay-real target+/-1 strict lock for src/state_flags.c x221C_u16_y visible-bit reset lane.
    #
    # Decomp refs for this lane:
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = int(case.target_record)
    owner = int(case.owner_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]

    # src/state_flags.c lane preconditions:
    # - owner is in CatchDashPull (0x00D7) where ChangeMotionState destination does not retain
    #   x221C_u16_y carry;
    # - seed/ref keep state_flags[3] byte parity at zero while legacy output had stale 0x01 set.
    assert int(seed["action_id"][owner]) == 215, case.note  # CatchDashPull
    assert int(ref["action_id"][owner]) == 215, case.note
    assert int(seed["on_ground"][owner]) == 1, case.note
    assert int(seed["hitlag"][owner]) == 0, case.note
    assert int(seed["hitstun"][owner]) == 0, case.note
    assert int(seed["action_frame"][owner]) + 1 == int(ref["action_frame"][owner]), case.note
    assert int(seed["state_flags"][owner][3]) == 0, case.note
    assert int(ref["state_flags"][owner][3]) == 0, case.note
    assert (int(seed["state_flags"][owner][2]) & 0x04) != 0, case.note

    # Strict transition lock coverage on both players for target-1 / target / target+1.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )
