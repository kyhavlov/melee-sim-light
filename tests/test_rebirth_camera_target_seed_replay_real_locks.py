from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _BlockerCase:
    dataset_rel: str
    target_record: int
    p: int
    expected_out_state_flags_4: int
    expected_ref_state_flags_4: int
    expected_target_x: float
    expected_target_y: float
    expected_target_z: float
    expected_radius: float
    note: str


@dataclass(frozen=True)
class _ControlCase:
    dataset_rel: str
    record: int
    p: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1920,
            p=1,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            expected_target_x=-49.639732,
            expected_target_y=86.5845,
            expected_target_z=0.07064841,
            expected_radius=11.2,
            note="F04 over-set late-Rebirth blocker: the missing camera target is a valid on-stage world point, not a replay-fit visibility toggle",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2710,
            p=0,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            expected_target_x=-158.48605,
            expected_target_y=89.19599,
            expected_target_z=0.044831384,
            expected_radius=11.2,
            note="F04 under-set dead-flow blocker: mixed-direction family still has a concrete camera target and radius on the seed row",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3227,
            p=0,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            expected_target_x=15.63973,
            expected_target_y=86.5845,
            expected_target_z=-0.07064841,
            expected_radius=11.2,
            note="F04 over-set late-Rebirth blocker on the opposite facing: the camera target mirrors with facing while the mismatch remains runtime-owned",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=705,
            p=1,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            expected_target_x=-169.22974,
            expected_target_y=18.22252,
            expected_target_z=0.025561402,
            expected_radius=11.2,
            note="F04 under-set blocker: seeded camera target stays causal on the damage-side family as well",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=4514,
            p=0,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            expected_target_x=15.083799,
            expected_target_y=86.3869,
            expected_target_z=-0.1099591,
            expected_radius=11.2,
            note="F04 over-set late-Rebirth blocker for Falco: the target point and radius differ by character data, not replay heuristics",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2534,
            p=0,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            expected_target_x=-86.196175,
            expected_target_y=117.39765,
            expected_target_z=0.025561402,
            expected_radius=11.2,
            note="F04 under-set blocker in the high-Y dead-flow family: explicit target seeding maps the mixed-direction rows to the same camera-subject contract",
        ),
    ],
)
def test_rebirth_camera_target_seed_locks_mixed_direction_blockers(case: _BlockerCase) -> None:
    # Foundational F04 blocker locks:
    # - ftLib_80086A8C reads camera-subject point/radius, not the replay-visible x221F_b0 bit alone.
    # - ftLib_800866DC writes the subject point from the camera zoom target bone plus co_attrs.x170.
    # - ftCamera_80076018 scales the camera-box radius from fighter camera data.
    # Runtime behavior is intentionally unchanged here; this bundle only promotes the hidden camera
    # target semantics needed for the later F04 runtime ownership follow-through.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_800866DC}
    # refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p
    seed = samples[case.target_record]["seed_t"]

    assert float(seed["camera_target_world_x_f32"][p]) == pytest.approx(case.expected_target_x, abs=1e-4), case.note
    assert float(seed["camera_target_world_y_f32"][p]) == pytest.approx(case.expected_target_y, abs=1e-4), case.note
    assert float(seed["camera_target_world_z_f32"][p]) == pytest.approx(case.expected_target_z, abs=1e-4), case.note
    assert float(seed["camera_box_radius_f32"][p]) == pytest.approx(case.expected_radius, abs=1e-4), case.note

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
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=1888,
            p=1,
            note="pre-Rebirth dead-flow control has no valid camera target pose yet, so the seeded target lanes must stay zero",
        ),
        _ControlCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=1889,
            p=1,
            note="the row before the first actual Rebirth seed still has no valid camera target source and must not be synthesized",
        ),
    ],
)
def test_rebirth_camera_target_seed_negative_controls(case: _ControlCase) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[case.record]["seed_t"]
    assert float(seed["camera_target_world_x_f32"][case.p]) == pytest.approx(0.0), case.note
    assert float(seed["camera_target_world_y_f32"][case.p]) == pytest.approx(0.0), case.note
    assert float(seed["camera_target_world_z_f32"][case.p]) == pytest.approx(0.0), case.note
    assert float(seed["camera_box_radius_f32"][case.p]) == pytest.approx(0.0), case.note
