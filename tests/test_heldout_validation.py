from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.eval.run_heldout_validation import (
    HeldoutReport,
    load_heldout_index,
    suite_cli_arg,
    validate_heldout_corpus,
    write_summary,
)
from tools.slippi.suite_io import repo_root


def test_heldout_index_lists_all_current_character_buckets() -> None:
    paths = load_heldout_index(Path("replays/suites/heldout.json"))
    names = {path.name for path in paths}

    assert names == {
        "marth_heldout.json",
        "sheik_heldout.json",
        "spacies_heldout.json",
    }
    assert "aggregate_recent.json" not in names


def test_heldout_index_rejects_aggregate_recent(tmp_path: Path) -> None:
    index = tmp_path / "heldout.json"
    index.write_text(json.dumps({"suites": ["replays/suites/aggregate_recent.json"]}))

    with pytest.raises(ValueError, match="aggregate_recent"):
        load_heldout_index(index)


def test_heldout_replays_are_not_lfs_tracked_assets() -> None:
    assert "replays/heldout/**/*.slpz" not in Path(".gitattributes").read_text()
    assert "!replays/heldout/**/*.slpz" not in Path(".gitignore").read_text()


def test_heldout_suite_cli_arg_is_repo_relative() -> None:
    suite = repo_root() / "replays/suites/marth_heldout.json"

    assert suite_cli_arg(suite) == "replays/suites/marth_heldout.json"


def test_heldout_report_headers_are_portable() -> None:
    report_dir = Path("reports/validation/heldout")
    reports = sorted(report_dir.glob("*_heldout_*.txt"))
    assert reports

    repo = repo_root().as_posix()
    for report in reports:
        header = "\n".join(report.read_text().splitlines()[:2])
        assert repo not in header
        assert "/mnt/" not in header


def test_heldout_missing_replay_reports_local_corpus_setup(tmp_path: Path) -> None:
    suite = tmp_path / "missing_replay_suite.json"
    suite.write_text(
        json.dumps(
            {
                "name": "missing_replay_suite",
                "replays": [
                    {
                        "replay": "replays/heldout/marth/missing_fixture.slpz",
                        "ports": [1, 2],
                        "stage_id": 32,
                    }
                ],
            }
        )
    )

    with pytest.raises(SystemExit) as exc_info:
        validate_heldout_corpus([suite])

    message = str(exc_info.value)
    assert "Missing held-out replay corpus file: replays/heldout/marth/missing_fixture.slpz" in message
    assert "Held-out replay blobs are local ignored data." in message
    assert "Restore/symlink/copy the local held-out corpus under replays/heldout/" in message


def test_heldout_summary_includes_every_report_row(tmp_path: Path) -> None:
    out = tmp_path / "summary.txt"
    text = write_summary(
        [
            HeldoutReport(
                suite="marth_heldout",
                replay_count=12,
                records=100,
                scored_lanes=1000,
                one_step_discrete=7,
                strict_discrete=9,
                rollout_first=3,
                rollout_seeded=2,
                stage_coverage="battlefield:2",
            ),
            HeldoutReport(
                suite="spacies_heldout",
                replay_count=12,
                records=200,
                scored_lanes=4000,
                one_step_discrete=4,
                strict_discrete=8,
                rollout_first=0,
                rollout_seeded=0,
                stage_coverage="final_destination:2",
            ),
        ],
        out,
    )

    assert "marth_heldout\t12\t100\t1000\t7\t7.000" in text
    assert "spacies_heldout\t12\t200\t4000\t4\t1.000" in text
    assert out.read_text() == text
