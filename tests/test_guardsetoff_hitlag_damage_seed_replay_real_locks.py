from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_seed_damage_min: int
    expected_out_action_frame: int
    expected_ref_action_frame: int
    expected_out_state_flags_3: int
    expected_ref_state_flags_3: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1212,
            p=1,
            expected_seed_damage_min=9,
            expected_out_action_frame=1,
            expected_ref_action_frame=2,
            expected_out_state_flags_3=0,
            expected_ref_state_flags_3=0,
            note="AGN blocker A carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1477,
            p=0,
            expected_seed_damage_min=9,
            expected_out_action_frame=1,
            expected_ref_action_frame=3,
            expected_out_state_flags_3=0,
            expected_ref_state_flags_3=0,
            note="AGN blocker B carries the GuardSetOff entry hitlag-damage lower bound through hitlag",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=2393,
            p=0,
            expected_seed_damage_min=1,
            expected_out_action_frame=10,
            expected_ref_action_frame=6,
            expected_out_state_flags_3=32,
            expected_ref_state_flags_3=32,
            note="AGN blocker C still carries the GuardSetOff entry hitlag-damage lower bound through powershield-active hitlag after the x221C_b1 lane is corrected",
        ),
    ],
)
def test_guardsetoff_hitlag_damage_seed_lane_locks_blockers_and_adjacent_controls(case: _Case) -> None:
    # Foundational F02 blocker locks:
    # - GuardSetOff entry rate in ftCo_80092F2C reads fp->x19A4, a hidden shield-hit int damage lane.
    # - Slippi does not expose x19A4 directly, so preprocessing now carries a causal lower bound from
    #   the segment's entry hitlag.
    # - These blocker rows remain runtime-mismatched in action_frame/state_flags[3]; the new seed lane
    #   is the schema handle for a future decomp-backed runtime fix.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        assert int(samples.shape[0]) > rec, f"dataset too short: record={rec}"
        seed = samples[rec]["seed_t"]
        assert int(seed["guard_setoff_hitlag_damage_min"][p]) == case.expected_seed_damage_min, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_target["action_frame"][p]) == case.expected_out_action_frame, case.note
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note
    assert int(out_target["state_flags"][p, 3]) == case.expected_out_state_flags_3, case.note
    assert int(ref_target["state_flags"][p, 3]) == case.expected_ref_state_flags_3, case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note
