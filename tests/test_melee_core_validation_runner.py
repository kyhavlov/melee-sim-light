from __future__ import annotations

import threading
from collections import Counter
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace

from tools.validation import validate_replay


ROOT = Path(__file__).resolve().parents[1]


def _result(*, passed: bool = True, frames: int = 10) -> dict[str, object]:
    return {
        "pass": passed,
        "frames": frames,
        "total": frames,
        "matched_frames": frames if passed else frames - 1,
        "mismatched_frames": 0 if passed else 1,
        "exact_prefix_frames": frames if passed else frames - 1,
        "strict_suffix_frames": frames if passed else 0,
        "runner_seconds": 0.01,
        "first_mismatch_frame": None if passed else 7,
        "last_mismatch_frame": None if passed else 7,
        "mismatch_count": 0 if passed else 1,
        "mismatch_fingerprint": None if passed else "0123456789abcdef",
        "actual_output_fingerprint": "1111222233334444",
        "mismatch_fields": []
        if passed
        else [{"field": "pos_x", "count": 1, "first_frame": 7, "last_frame": 7}],
        "render_visibility_mismatch_count": 0,
        "first_render_visibility_mismatch_frame": None,
        "signed_zero_equal_count": 0,
        "details": []
        if passed
        else [{"frame": 7, "field": "pos_x", "expected": "1", "actual": "2"}],
    }


def test_canonical_phase5_scope_selects_32_replays_without_a_duplicate_manifest() -> None:
    suite, cases = validate_replay.load_suite_cases(
        ROOT / "replays/suites/aggregate_recent.json",
        characters=frozenset(("fox", "falco")),
        stages=frozenset((2, 3, 8, 28, 31, 32)),
    )

    assert suite.name == "aggregate_recent"
    assert len(cases) == 32
    assert Counter(case.stage_id for case in cases) == {
        32: 16,
        3: 4,
        31: 3,
        2: 3,
        8: 4,
        28: 2,
    }
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

    classifications = validate_replay.load_classifications(
        ROOT / "replays/suites/melee_core_classifications.json"
    )
    assert classifications
    supported_replays = {
        replay.replay
        for replay in validate_replay.load_suite(
            ROOT / "replays/suites/melee_core_aggregate.json"
        ).replays
    }
    assert set(classifications) <= supported_replays
    marth = classifications[
        "replays/validation/marth/InternalPowerlessWallaby.slpz"
    ]
    assert marth.expected["ppc"] is marth.expected["native"]


def test_melee_core_aggregate_filters_pending_characters_without_losing_inventory() -> None:
    suite, cases = validate_replay.load_suite_cases(
        ROOT / "replays/suites/melee_core_aggregate.json",
        characters=frozenset(
            (
                "fox",
                "falco",
                "marth",
                "captain falcon",
                "sheik",
                "zelda",
                "jigglypuff",
            )
        ),
        stages=frozenset((2, 3, 8, 28, 31, 32)),
    )

    assert suite.name == "melee_core_aggregate"
    assert len(suite.replays) == 595
    assert len(cases) == 133
    assert all("Peach" not in case.characters for case in cases)


def test_suite_replay_execution_metadata_reaches_native_validator(
    monkeypatch, tmp_path: Path
) -> None:
    original_metadata = {"source": "legacy-capture"}
    game = SimpleNamespace(frames=object(), start={}, metadata=original_metadata)
    captured: dict[str, object] = {}

    @contextmanager
    def fake_replay_path(_path: Path):
        yield tmp_path / "legacy.slp"

    class FakeNative:
        @staticmethod
        def validate_replay(_frames, _start, metadata, **_kwargs):
            captured.update(metadata)
            return {}

    monkeypatch.setattr(validate_replay, "replay_path_for_peppi", fake_replay_path)
    monkeypatch.setattr(validate_replay, "_read_slippi", lambda _path, _flag: game)
    monkeypatch.setattr(validate_replay, "game_data_dir", lambda: tmp_path)

    validate_replay.validate_one(
        FakeNative(),
        tmp_path / "legacy.slpz",
        frames=0,
        start_frame=None,
        timeout=1.0,
        backend="native",
        played_on="network",
    )

    assert original_metadata == {"source": "legacy-capture"}
    assert captured == {"source": "legacy-capture", "playedOn": "network"}


def test_puff_legacy_capture_profiles_are_explicit() -> None:
    suite = validate_replay.load_suite(ROOT / "replays/suites/puff.json")
    by_name = {Path(entry.replay).name: entry for entry in suite.replays}

    diamond = by_name["diamond-diamond-78251f79d12df3bcdb1d4abe.slpz"]
    master = by_name["master-diamond-d514ba67193e53290c8d1b83.slpz"]
    assert diamond.played_on == "network"
    assert diamond.ucf_cardinals_1_0_enabled is None
    assert master.played_on == "network"
    assert master.ucf_cardinals_1_0_enabled is False


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

    class FakeRunnerPool:
        discarded = False

        def __init__(self, _count: int) -> None:
            self.runner = object()

        def acquire(self):
            return self.runner

        def release(self, _runner) -> None:
            pass

        def discard(self, _runner) -> None:
            type(self).discarded = True

        def close(self) -> None:
            pass

    monkeypatch.setattr(validate_replay, "validate_one", fake_validate_one)
    monkeypatch.setattr(validate_replay, "game_data_dir", lambda: tmp_path)
    monkeypatch.setattr(validate_replay, "_NativeRunnerPool", FakeRunnerPool)

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
    assert FakeRunnerPool.discarded


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
    assert "first=7 last=7 fields=1" in output
    assert "pass=1 classified=0 xpass=0 fail=1 error=1 frames=20" in output
    assert "aggregate_fps=400" in output


def test_exact_classification_passes_and_drift_fails(capsys) -> None:
    case = validate_replay.ReplayCase(Path("known.slpz"), "known.slpz")
    result = _result(passed=False)
    compact_snapshot = validate_replay.classification_snapshot(result)
    compact_snapshot[validate_replay.CLASSIFICATION_COMPACT_FIELD_KEY] = (
        validate_replay.mismatch_fields_digest(
            compact_snapshot.pop("mismatch_fields")
        )
    )
    classification = validate_replay.ReplayClassification(
        replay="known.slpz",
        classification_id="known-owner",
        owner="source owner",
        rationale="bounded source-classified residual",
        sources=("refs/melee/src/owner.c::owner",),
        expected={"native": compact_snapshot},
    )
    outcome = validate_replay.ReplayOutcome(case, result, None, 0.02)

    assert validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        classifications={case.display_path: classification},
    )
    output = capsys.readouterr().out
    assert "CLASS" in output
    assert "classified=1" in output

    legacy_case = validate_replay.ReplayCase(Path("known.slpz"), "known.slp")
    legacy_outcome = validate_replay.ReplayOutcome(legacy_case, result, None, 0.02)
    status, _ = validate_replay._result_status(
        "native",
        legacy_outcome,
        {"known.slpz": classification},
        strict_classifications=False,
    )
    assert status == "classified"

    result["mismatch_fingerprint"] = "fedcba9876543210"
    assert not validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        classifications={case.display_path: classification},
    )
    assert "classification drift" in capsys.readouterr().out


def test_full_replay_xpass_rejects_stale_classification(capsys) -> None:
    case = validate_replay.ReplayCase(Path("known.slpz"), "known.slpz")
    failed_result = _result(passed=False)
    classification = validate_replay.ReplayClassification(
        replay="known.slpz",
        classification_id="known-owner",
        owner="source owner",
        rationale="bounded source-classified residual",
        sources=("refs/melee/src/owner.c::owner",),
        expected={"native": validate_replay.classification_snapshot(failed_result)},
    )
    outcome = validate_replay.ReplayOutcome(case, _result(), None, 0.02)

    assert not validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        classifications={case.display_path: classification},
    )
    assert "XPASS" in capsys.readouterr().out

    partial = _result(frames=5)
    partial["total"] = 10
    outcome = validate_replay.ReplayOutcome(case, partial, None, 0.02)
    assert validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        classifications={case.display_path: classification},
    )
    output = capsys.readouterr().out
    assert "PASS" in output
    assert "XPASS" not in output


def test_full_output_lock_rejects_behavior_drift(capsys) -> None:
    case = validate_replay.ReplayCase(Path("known.slpz"), "known.slpz")
    result = _result()
    outcome = validate_replay.ReplayOutcome(case, result, None, 0.02)
    lock = validate_replay.ReplayOutputLock(
        replay=case.display_path,
        expected={
            "native": {
                "frames": 10,
                "actual_output_fingerprint": "1111222233334444",
            }
        },
    )

    assert validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        output_locks={case.display_path: lock},
        require_output_lock=True,
    )
    result["actual_output_fingerprint"] = "aaaaaaaaaaaaaaaa"
    assert not validate_replay.print_backend_results(
        "native",
        [outcome],
        workers=1,
        wall_seconds=0.02,
        show_timing=False,
        output_locks={case.display_path: lock},
        require_output_lock=True,
    )
    assert "output drift" in capsys.readouterr().out
