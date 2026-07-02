from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    target_record: int
    p: int
    expected_target_phase: int
    expected_target_plus1_phase: int
    expected_ref_action_frame: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=1212,
            p=1,
            expected_target_phase=2,
            expected_target_plus1_phase=3,
            expected_ref_action_frame=2,
            note="AGN row A is the last-hitlag GuardSetOff row before the first post-hitlag ownership handoff row",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=1477,
            p=0,
            expected_target_phase=2,
            expected_target_plus1_phase=3,
            expected_ref_action_frame=3,
            note="AGN row B follows the same last-hitlag -> first-post-hitlag GuardSetOff ownership pattern",
        ),
        _Case(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=2393,
            p=0,
            expected_target_phase=2,
            expected_target_plus1_phase=3,
            expected_ref_action_frame=6,
            note="AGN row C is also a last-hitlag GuardSetOff seed row even when powershield state diverges",
        ),
    ],
)
def test_guardsetoff_hitlag_exit_phase_seed_locks_blockers_and_adjacent_controls(case: _Case) -> None:
    # Foundational F02 handoff lock:
    # - ftCo_80092F2C shapes GuardSetOff entry rate before the frozen hitlag tail.
    # - Fighter_8006A360 advances ftAnim before ftCo_GuardSetOff_Anim resumes callback-owned rate
    #   on the first post-hitlag GuardSetOff row.
    # - This seed lane names that handoff phase, while the causal frame-speed derivation makes the
    #   last-hitlag row use the same action-frame owner as replay.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p

    assert int(samples[case.target_record - 2]["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][p]) == 1, case.note
    assert int(samples[case.target_record - 1]["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][p]) == 1, case.note
    assert int(samples[case.target_record]["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][p]) == case.expected_target_phase, case.note
    assert int(samples[case.target_record + 1]["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][p]) == case.expected_target_plus1_phase, case.note
    assert int(samples[case.target_record + 2]["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][p]) == 0, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(ref_target["action_frame"][p]) == case.expected_ref_action_frame, case.note
    assert int(out_target["action_frame"][p]) == int(ref_target["action_frame"][p]), case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]), case.note


@pytest.mark.integration
def test_guardsetoff_hitlag_exit_phase_seed_negative_control_outside_handoff() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[1214]["seed_t"]
    assert int(seed["guard_setoff_hitlag_exit_phase_u8"][1]) == 0
