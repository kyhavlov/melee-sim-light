from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from tools.eval.capturewait_owner_tick_forensics import (
    classify_capturewait_owner_tick_case,
    collect_capturewait_owner_tick_cases,
)


def test_classify_capturewait_owner_tick_case_distinguishes_modeled_vs_blocker() -> None:
    assert (
        classify_capturewait_owner_tick_case(
            owner_p=1,
            victim_p=0,
            owner_seed_action=0x00D8,
            owner_seed_action_frame=1,
            owner_ref_action=0x00D8,
            owner_ref_action_frame=2,
            victim_seed_action=0x00E3,
            victim_seed_action_frame=1,
            victim_ref_action=0x00E3,
            victim_ref_action_frame=3,
        )
        == "lower_slot_extra_tick_modeled"
    )
    assert (
        classify_capturewait_owner_tick_case(
            owner_p=0,
            victim_p=1,
            owner_seed_action=0x00D8,
            owner_seed_action_frame=0,
            owner_ref_action=0x00D8,
            owner_ref_action_frame=1,
            victim_seed_action=0x00E3,
            victim_seed_action_frame=1,
            victim_ref_action=0x00E3,
            victim_ref_action_frame=2,
        )
        == "steady_capturewait_no_extra_tick_blocker"
    )


@pytest.mark.integration
def test_capturewait_owner_tick_forensics_finds_modeled_and_blocker_rows() -> None:
    root = Path(__file__).resolve().parents[1]
    expected = (
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            (981, 1, 0),
            "steady_capturewait_no_extra_tick_blocker",
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            (447, 0, 1),
            "steady_capturewait_no_extra_tick_blocker",
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            (5911, 0, 1),
            "steady_capturewait_no_extra_tick_blocker",
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            (8257, 1, 0),
            "lower_slot_extra_tick_modeled",
        ),
    )
    for dataset_rel, key, want in expected:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        keyed = {
            (case.record, case.owner_p, case.victim_p): case.classification
            for case in collect_capturewait_owner_tick_cases(dataset_path)
        }
        assert keyed[key] == want


@pytest.mark.integration
def test_capturewait_owner_tick_forensics_cli_reports_exact_rows() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    proc = subprocess.run(
        [
            "uv",
            "run",
            "python",
            "-m",
            "tools.eval.capturewait_owner_tick_forensics",
            "--dataset",
            str(dataset_path),
            "--format",
            "tsv",
        ],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    stdout = proc.stdout
    assert "record\towner_p\tvictim_p" in stdout
    assert "8257\t1\t0\t216\t1\t216\t2\t227\t1\t227\t3\towner_later\tlower_slot_extra_tick_modeled" in stdout
