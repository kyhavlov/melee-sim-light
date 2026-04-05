from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    port: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl"
        ),
        target_record=2259,
        port=0,
        note="AGN fresh DamageFlyHi entry raises x221A_b3 during hitlag",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        target_record=5088,
        port=0,
        note="GAT fresh DamageFlyN entry raises x221A_b3 during hitlag",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        target_record=6012,
        port=0,
        note="GAT fresh DamageFlyN entry raises x221A_b3 during hitlag on the later combo row",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.target_record}-p{c.port}")
def test_fresh_damage_entry_rows_raise_221a_b3(case: _Case) -> None:
    # Replay-real lock for fresh Damage* entry x221A_b3:
    # - Fighter_ProcessHit sets x221A_b3 on the knockback-owning hitlag path,
    # - Fighter_8006A1BC clears it when hitlag ends,
    # - so first visible Damage* entry rows with active hitlag/hitstun should expose bit0x10.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.port
    target = samples[case.target_record]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][p]) != int(ref["action_id"][p]), case.note
    assert 87 <= int(ref["action_id"][p]) <= 90, case.note
    assert int(ref["action_frame"][p]) == 1, case.note
    assert int(ref["hitlag"][p]) > 0, case.note
    assert int(ref["hitstun"][p]) > 0, case.note
    assert int(ref["state_flags"][p, 1]) == 48, case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert int(out_row["state_flags"][p, 1]) == int(ref_row["state_flags"][p, 1]), case.note
