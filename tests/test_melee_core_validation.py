from __future__ import annotations

import os
from pathlib import Path

import pytest

from tools.validation import validate_replay
from tools.validation.validate_replay import (
    NATIVE,
    NATIVE_BINARY,
    PPC_BINARY,
    QEMU,
    ReplayCase,
    load_native,
    load_suite_cases,
    run_cases,
    validate_one,
)


ROOT = Path(__file__).resolve().parents[1]
STARTER_REPLAY = (
    ROOT
    / "replays"
    / "validation"
    / "aggregate_recent"
    / "PutridJoyousOryx.slpz"
)


@pytest.mark.parametrize("backend", ["ppc", "native"])
def test_validation_streams_starter_replay_from_arrow(backend: str) -> None:
    binary = NATIVE_BINARY if backend == "native" else PPC_BINARY
    required = [STARTER_REPLAY, NATIVE, binary]
    if backend == "ppc":
        required.append(QEMU)
    if not all(path.is_file() for path in required):
        pytest.skip(f"{backend} core validation artifacts are unavailable")

    result = validate_one(
        load_native(),
        # Preserve legacy .slp callers when storage has moved to .slpz.
        STARTER_REPLAY.with_suffix(".slp"),
        frames=0,
        start_frame=None,
        timeout=20.0,
        backend=backend,
    )

    assert result["pass"] is True
    assert result["frames"] == 7543
    assert result["available"] == 7543
    assert result["raw_frames"] == 7549
    assert result["seed_frame"] == -123
    assert result["first_ref_frame"] == -122
    assert result["matched_frames"] == 7543
    assert result["mismatched_frames"] == 0
    assert result["exact_prefix_frames"] == 7543
    assert result["strict_suffix_frames"] == 7543
    assert float(result["runner_seconds"]) > 0.0
    assert float(result["end_to_end_seconds"]) >= float(result["runner_seconds"])
    assert result["render_visibility_mismatch_count"] == 0
    assert result["first_render_visibility_mismatch_frame"] is None
    assert result["signed_zero_equal_count"] == 0
    assert result["first_mismatch_frame"] is None
    assert result["last_mismatch_frame"] is None
    assert result["mismatch_count"] == 0
    assert result["mismatch_fingerprint"] is None
    assert result["mismatch_fields"] == []
    assert result["details"] == []


def test_native_validation_replaces_interrupted_server(monkeypatch) -> None:
    if not all(path.is_file() for path in (STARTER_REPLAY, NATIVE, NATIVE_BINARY)):
        pytest.skip("native core validation artifacts are unavailable")

    runners = []

    def interrupt_first_job(native, replay, **kwargs):
        runner = kwargs["runner"]
        runners.append(runner)
        if len(runners) == 1:
            # Leave an incomplete header in the pipe, as a write timeout can.
            # Inject the interruption so the test doesn't depend on CPU speed.
            os.write(runner.stdin_fd, b"\x00")
            raise TimeoutError("interrupted job header")
        return validate_one(native, replay, **kwargs)

    monkeypatch.setattr(validate_replay, "validate_one", interrupt_first_job)
    case = ReplayCase(STARTER_REPLAY, str(STARTER_REPLAY))
    outcomes, _ = run_cases(
        load_native(),
        [case, case],
        workers=1,
        frames=16,
        start_frame=None,
        timeout=30.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert outcomes[0].error == "TimeoutError: interrupted job header"
    assert outcomes[1].error is None
    assert outcomes[1].result["pass"] is True
    assert outcomes[1].result["frames"] == 16
    assert runners[0].process.pid != runners[1].process.pid
    assert all(runner.process.poll() is not None for runner in runners)


def test_native_validation_runs_current_oracle_replays_in_parallel() -> None:
    required = [NATIVE, NATIVE_BINARY]
    if not all(path.is_file() for path in required):
        pytest.skip("native core validation artifacts are unavailable")
    _suite, cases = load_suite_cases(
        ROOT / "replays/suites/aggregate_recent.json",
        characters=frozenset(("fox",)),
        stages=frozenset((32,)),
    )

    outcomes, wall_seconds = run_cases(
        load_native(),
        cases,
        # Bound immutable data copies on memory-limited CI runners.
        workers=min(4, len(cases)),
        frames=256,
        start_frame=None,
        timeout=3.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert len(outcomes) == 3
    assert all(outcome.error is None for outcome in outcomes)
    assert all(outcome.result is not None and outcome.result["pass"] for outcome in outcomes)
    assert wall_seconds < 3.0
