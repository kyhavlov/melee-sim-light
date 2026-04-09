from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def _assert_branch_identity_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "hitstun",
    ):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


@dataclass(frozen=True)
class _Case:
    record: int
    p: int


_DATASET_REL = (
    "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
    "QuerulousGrandDinosaur.msl"
)
_CASES = [
    _Case(9026, 1),  # CliffWait control
    _Case(9027, 1),  # CliffWait -> CliffJumpQuick1 entry, same-proc attach snap
    _Case(9028, 1),  # CliffJumpQuick1 continuation
    _Case(9034, 1),  # mid Jump1 continuation
    _Case(9040, 1),  # last steady Jump1 row before Jump2 handoff
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES)
def test_cliffjump1_attach_snap_replay_real_rows_exact(case: _Case) -> None:
    # Replay-real lock for CliffJump1 attach ownership:
    # - ftCo_8009B1B8 enters CliffJump1, immediately ticks the new motion, then calls
    #   ftCo_CliffCatch_Phys in the same proc.
    # - CliffJump1_Phys is a direct call-through to ftCo_CliffCatch_Phys on steady Jump1 rows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c::{
    #   ftCo_8009B1B8,ftCo_CliffJump1_Phys,ftCo_CliffJump1_Anim,ftCo_8009B2F8}
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / _DATASET_REL
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.p, rng_damage_fly_roll_gate=True)
    _assert_branch_identity_match_ref(out_row=out_row, ref_row=ref_row, p=case.p)
    assert float(out_row["pos_x"][case.p]) == pytest.approx(float(ref_row["pos_x"][case.p]), abs=1e-6)
    assert float(out_row["pos_y"][case.p]) == pytest.approx(float(ref_row["pos_y"][case.p]), abs=1e-6)
