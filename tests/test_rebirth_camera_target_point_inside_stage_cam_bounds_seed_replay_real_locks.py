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
    expected_inside: int
    expected_out_state_flags_4: int
    expected_ref_state_flags_4: int
    note: str


@dataclass(frozen=True)
class _ControlCase:
    dataset_rel: str
    record: int
    p: int
    expected_inside: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=1920,
            p=1,
            expected_inside=1,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="late-Rebirth over-set blocker still has a valid inside-camera target point on the seed row",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=2710,
            p=0,
            expected_inside=1,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="mixed-direction under-set blocker is also point-inside on the seed row, so the branch input alone does not resolve ownership",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3227,
            p=0,
            expected_inside=1,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="mirrored late-Rebirth blocker stays point-inside under the same Camera_80030CD8-style predicate",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=705,
            p=1,
            expected_inside=1,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="damage-side under-set blocker keeps the inside-point predicate even though the visible bit diverges",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=4514,
            p=0,
            expected_inside=1,
            expected_out_state_flags_4=128,
            expected_ref_state_flags_4=0,
            note="Falco late-Rebirth over-set blocker remains point-inside on the seed row",
        ),
        _BlockerCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2534,
            p=0,
            expected_inside=0,
            expected_out_state_flags_4=0,
            expected_ref_state_flags_4=128,
            note="high-Y dead-flow under-set blocker stays outside the top camera bound even though the camera target exists",
        ),
    ],
)
def test_rebirth_camera_target_point_inside_stage_cam_bounds_seed_locks_blockers(case: _BlockerCase) -> None:
    # Foundational F04 blocker lock:
    # - ftLib_80086A8C first clears fp->x221F_b0 when Camera_80030CD8 reports the camera-subject
    #   point is already on-screen.
    # - This bundle promotes that point-inside branch input as a named seed lane without changing
    #   runtime behavior, so the mixed over/under blocker rows can be grouped under the same
    #   decomp-owned predicate.
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
    # data/stages/final_destination.json: cam_bounds_world
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    seed = samples[case.target_record]["seed_t"]
    p = case.p

    assert int(seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == case.expected_inside, case.note

    _, ref_target, out_target = _run_one_step_row(dataset_path, case.target_record, p)
    assert int(out_target["state_flags"][p, 4]) == case.expected_out_state_flags_4, case.note
    assert int(ref_target["state_flags"][p, 4]) == case.expected_ref_state_flags_4, case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ControlCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=1888,
            p=1,
            expected_inside=0,
            note="pre-Rebirth control has no valid camera target/radius yet, so the point-inside lane must stay zero",
        ),
        _ControlCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            record=1889,
            p=1,
            expected_inside=0,
            note="row before the first actual Rebirth target source remains zero",
        ),
    ],
)
def test_rebirth_camera_target_point_inside_stage_cam_bounds_seed_negative_controls(case: _ControlCase) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[case.record]["seed_t"]
    assert int(seed["camera_target_point_inside_stage_cam_bounds_u8"][case.p]) == case.expected_inside, case.note
