from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _PassiveWallJumpCase:
    record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _PassiveWallJumpCase(
            record=8405,
            note="QGD PassiveWallJump entry-hold family A target row",
        ),
        _PassiveWallJumpCase(
            record=8406,
            note="QGD PassiveWallJump entry-hold family A target+1 row",
        ),
        _PassiveWallJumpCase(
            record=9219,
            note="QGD PassiveWallJump entry-hold family B target row",
        ),
        _PassiveWallJumpCase(
            record=9220,
            note="QGD PassiveWallJump entry-hold family B target+1 row",
        ),
    ],
)
def test_passivewalljump_entry_hold_qgd_replay_real_lock(case: _PassiveWallJumpCase) -> None:
    # Replay-real lock for PassiveWallJump entry hold on the first post-entry snapshots.
    #
    # Decomp:
    # - ftCo_PassiveWall_Anim transitions through inlineA0 into ftCo_MS_PassiveWallJump.
    # - inlineA0 calls Fighter_ChangeMotionState(..., anim_start=fp->cur_anim_frame, anim_speed=1.0f)
    #   without a local ftAnim_8006EBA4 tick in that callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{inlineA0,ftCo_PassiveWall_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 203, case.note  # ftCo_MS_PassiveWallJump
    assert int(ref["action_id"][p]) == 203, case.note
    assert int(seed["animation_index"][p]) == 203, case.note  # ftCo_SM_PassiveWallJump
    assert int(ref["animation_index"][p]) == 203, case.note
    assert int(seed["action_frame"][p]) == 0, case.note
    assert int(ref["action_frame"][p]) == 0, case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(ref["hitlag"][p]) == 0, case.note
    assert int(seed["hitstun"][p]) == 0, case.note
    assert int(ref["hitstun"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "on_ground"):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"
