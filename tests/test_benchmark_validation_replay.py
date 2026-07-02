from __future__ import annotations

import json
import sys
from pathlib import Path

from tools.eval import benchmark_validation_replay as bench


def _fake_case(suite_path: Path) -> dict:
    return {
        "suite_path": suite_path.as_posix(),
        "suite_name": "smoke",
        "replay": "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        "replay_label": "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        "ports": [1, 2],
        "stage_id": 3,
        "characters": {"1": "fox", "2": "falco"},
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": False,
    }


def _fake_row(case: dict, **_kwargs) -> dict:
    return {
        "suite": case["suite_name"],
        "suite_path": case["suite_path"],
        "replay": case["replay_label"],
        "ports": "1,2",
        "stage_id": case["stage_id"],
        "characters": "{}",
        "records": 10,
        "build_wall_s": 0.01,
        "one_step_native_init_wall_s": 0.001,
        "one_step_native_reseed_wall_s": 0.002,
        "one_step_native_step_wall_s": 0.003,
        "one_step_native_write_wall_s": 0.004,
        "one_step_wall_s": 0.02,
        "rollout_wall_s": 0.03,
        "total_wall_s": 0.06,
        "build_records_per_s": 1000.0,
        "one_step_records_per_s": 500.0,
        "rollout_records_per_s": 333.0,
        "total_records_per_s": 166.0,
        "one_step_discrete_mismatch": 1,
        "rollout_first_mismatch_total": 2,
        "float_top_errors": 3,
        "float_top_downstream": 4,
    }


def _assert_outputs(out_dir: Path) -> None:
    summary_tsv = out_dir / "summary.tsv"
    summary_json = out_dir / "summary.json"
    assert summary_tsv.exists()
    assert summary_json.exists()
    assert (out_dir / "outliers_total_wall.tsv").exists()
    text = summary_tsv.read_text(encoding="utf-8")
    assert "one_step_native_reseed_wall_s" in text
    assert "rollout_records_per_s" in text
    payload = json.loads(summary_json.read_text(encoding="utf-8"))
    assert payload["totals"]["replays"] == 1
    assert payload["totals"]["records"] == 10


def test_benchmark_validation_replay_suite_cli_writes_summary(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.setattr(bench, "_cases_from_suite", lambda suite_path: [_fake_case(Path(suite_path))])
    monkeypatch.setattr(bench, "_run_case", _fake_row)
    out_dir = tmp_path / "suite"
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "benchmark_validation_replay",
            "--suite",
            "replays/suites/fox_falco_fd_ucf084_recent.json",
            "--limit",
            "1",
            "--out-dir",
            str(out_dir),
        ],
    )

    bench.main()

    _assert_outputs(out_dir)


def test_benchmark_validation_replay_heldout_cli_writes_summary(monkeypatch, tmp_path: Path) -> None:
    monkeypatch.setattr(bench, "load_heldout_index", lambda _path: [Path("replays/suites/sheik.json")])
    monkeypatch.setattr(bench, "_cases_from_suite", lambda suite_path: [_fake_case(Path(suite_path))])
    monkeypatch.setattr(bench, "_run_case", _fake_row)
    out_dir = tmp_path / "heldout"
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "benchmark_validation_replay",
            "--heldout-index",
            "replays/heldout/index.json",
            "--limit",
            "1",
            "--out-dir",
            str(out_dir),
        ],
    )

    bench.main()

    _assert_outputs(out_dir)
