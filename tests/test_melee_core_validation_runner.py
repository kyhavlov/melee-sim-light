from __future__ import annotations

import threading
from collections import Counter
from pathlib import Path

from tools.melee_core import validate_replay


ROOT = Path(__file__).resolve().parents[1]


def _result(*, passed: bool = True, frames: int = 10) -> dict[str, object]:
    return {
        "pass": passed,
        "frames": frames,
        "matched_frames": frames if passed else frames - 1,
        "runner_seconds": 0.01,
        "first_mismatch_frame": None if passed else 7,
        "mismatch_count": 0 if passed else 1,
        "details": []
        if passed
        else [{"field": "pos_x", "expected": "1", "actual": "2"}],
    }


def test_canonical_phase5_scope_selects_23_replays_without_a_duplicate_manifest() -> None:
    suite, cases = validate_replay.load_suite_cases(
        ROOT / "replays/suites/aggregate_recent.json",
        characters=frozenset(("fox", "falco")),
        stages=frozenset((3, 31, 32)),
    )

    assert suite.name == "aggregate_recent"
    assert len(cases) == 23
    assert Counter(case.stage_id for case in cases) == {32: 16, 3: 4, 31: 3}
    assert all(set(case.characters) <= {"Fox", "Falco"} for case in cases)
    assert [
        case.replay.name
        for case in cases
        if case.stage_id == 32 and set(case.characters) == {"Fox"}
    ] == [
        "BlondHardHippopotamus.slpz",
        "FavorableSuperficialPig.slpz",
        "Game_20260514T181413.slpz",
        "HungryImportantSnake.slpz",
        "PutridJoyousOryx.slpz",
    ]


def test_parallel_runner_preserves_manifest_order_and_isolates_errors(
    monkeypatch, tmp_path: Path
) -> None:
    barrier = threading.Barrier(2)
    cases = [
        validate_replay.ReplayCase(tmp_path / name, name)
        for name in ("first.slp", "second.slp", "third.slp")
    ]

    def fake_validate_one(_native, replay, **_kwargs):
        if replay.name in {"first.slp", "second.slp"}:
            barrier.wait(timeout=1.0)
        if replay.name == "third.slp":
            raise RuntimeError("unsupported owner")
        return _result()

    monkeypatch.setattr(validate_replay, "validate_one", fake_validate_one)
    monkeypatch.setattr(validate_replay, "game_data_dir", lambda: tmp_path)

    outcomes, _wall = validate_replay.run_cases(
        object(),
        cases,
        workers=2,
        frames=0,
        start_frame=None,
        timeout=1.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert [outcome.case.display_path for outcome in outcomes] == [
        "first.slp",
        "second.slp",
        "third.slp",
    ]
    assert [outcome.error is None for outcome in outcomes] == [True, True, False]
    assert outcomes[2].error == "RuntimeError: unsupported owner"


def test_parallel_worker_auto_count_is_bounded(monkeypatch) -> None:
    monkeypatch.setattr(validate_replay.os, "cpu_count", lambda: 32)
    assert validate_replay._resolve_worker_count(0, 23) == 16
    assert validate_replay._resolve_worker_count(8, 23) == 8
    assert validate_replay._resolve_worker_count(64, 3) == 3


def test_suite_stdout_is_compact_and_reports_aggregate_throughput(capsys) -> None:
    cases = [
        validate_replay.ReplayCase(
            Path(name), name, (1, 2), 32, ("Fox", "Fox")
        )
        for name in ("pass.slpz", "fail.slpz", "error.slpz")
    ]
    outcomes = [
        validate_replay.ReplayOutcome(cases[0], _result(), None, 0.02),
        validate_replay.ReplayOutcome(cases[1], _result(passed=False), None, 0.02),
        validate_replay.ReplayOutcome(cases[2], None, "RuntimeError: loud stub", 0.02),
    ]

    passed = validate_replay.print_backend_results(
        "native", outcomes, workers=3, wall_seconds=0.05, show_timing=True
    )
    output = capsys.readouterr().out

    assert passed is False
    assert "PASS" in output
    assert "FAIL" in output
    assert "ERROR" in output
    assert "first=7 fields=1" in output
    assert "pass=1 fail=1 error=1 frames=20" in output
    assert "aggregate_fps=400" in output
