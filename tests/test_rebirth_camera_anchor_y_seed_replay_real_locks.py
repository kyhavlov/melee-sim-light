from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _BlockerCase:
    dataset_rel: str
    target_record: int
    p: int
    expected_out_state_flags_4: int
    expected_ref_state_flags_4: int
    note: str


@dataclass(frozen=True)
class _ControlCase:
    dataset_rel: str
    record: int
    p: int
    expected_anchor_y: float
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlockerCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            target_record=1920,
            p=1,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="AGN late-Rebirth blocker keeps camera visibility latched while the hidden Rebirth anchor Y should stay on the FD respawn platform",
        ),
        _BlockerCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            target_record=3227,
            p=0,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="GAT late-Rebirth blocker still mismatches after the camera-box seed-bit promotion; the hidden anchor Y remains the missing ownership input",
        ),
        _BlockerCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            target_record=4514,
            p=0,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="QGD late-Rebirth blocker clears visibility on ref_t1 while the seed still needs the respawn-anchor Y that ftCo_Rebirth_Cam owns",
        ),
    ],
)
def test_rebirth_camera_anchor_y_seed_lane_locks_blockers(case: _BlockerCase) -> None:
    # Foundational F04 blocker locks:
    # - ftCo_Rebirth_Cam writes camera subject Y from `fp->mv.co.common.x8` plus a camera offset.
    # - On FD, that hidden base lane is the respawn-point Y from stage data, not fighter cur_pos.y.
    # - Runtime behavior is intentionally unchanged here; the lane exists to unblock the later F04
    #   ownership fix while preserving the current blocker mismatch.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    # data/stages/final_destination.json: respawn_points
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = case.p

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
        seed = samples[rec]["seed_t"]
        assert float(seed["rebirth_camera_anchor_y_f32"][p]) == pytest.approx(45.0), case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_target["state_flags"][p, 4]) == case.expected_out_state_flags_4, case.note
    assert int(ref_target["state_flags"][p, 4]) == case.expected_ref_state_flags_4, case.note

    for rec in (case.target_record - 1, case.target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), case.note
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), case.note
        assert [int(x) for x in out_row["state_flags"][p]] == [int(x) for x in ref_row["state_flags"][p]], case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ControlCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=1888,
            p=1,
            expected_anchor_y=0.0,
            note="non-Rebirth dead-flow control must not synthesize the hidden Rebirth camera anchor",
        ),
        _ControlCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=1889,
            p=1,
            expected_anchor_y=0.0,
            note="pre-Rebirth control stays zero until the first actual Rebirth seed row",
        ),
    ],
)
def test_rebirth_camera_anchor_y_seed_lane_negative_controls(case: _ControlCase) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[case.record]["seed_t"]
    assert float(seed["rebirth_camera_anchor_y_f32"][case.p]) == pytest.approx(case.expected_anchor_y), case.note
