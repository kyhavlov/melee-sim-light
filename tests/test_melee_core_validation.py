from __future__ import annotations

from pathlib import Path

import pytest

from tools.melee_core.validate_replay import (
    DEFAULT_CLASSIFICATIONS,
    NATIVE,
    NATIVE_BINARY,
    PPC_BINARY,
    QEMU,
    ReplayCase,
    classification_matches,
    load_classifications,
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
    / "Game_20260514T181413.slpz"
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
    assert result["frames"] == 2723
    assert result["available"] == 2723
    assert result["raw_frames"] == 2724
    assert result["seed_frame"] == -123
    assert result["first_ref_frame"] == -122
    assert result["matched_frames"] == 2723
    assert result["mismatched_frames"] == 0
    assert result["exact_prefix_frames"] == 2723
    assert result["strict_suffix_frames"] == 2723
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
        workers=len(cases),
        frames=256,
        start_frame=None,
        timeout=3.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert len(outcomes) == 5
    assert all(outcome.error is None for outcome in outcomes)
    assert all(outcome.result is not None and outcome.result["pass"] for outcome in outcomes)
    assert wall_seconds < 3.0


def test_native_validation_compares_complete_classified_replays() -> None:
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native core validation artifacts are unavailable")
    classifications = load_classifications(DEFAULT_CLASSIFICATIONS)
    cases = [
        ReplayCase(ROOT / replay, replay)
        for replay in classifications
    ]

    outcomes, wall_seconds = run_cases(
        load_native(),
        cases,
        workers=len(cases),
        frames=0,
        start_frame=None,
        timeout=3.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert all(outcome.error is None for outcome in outcomes)
    for outcome in outcomes:
        assert outcome.result is not None
        expected = classifications[outcome.case.display_path].expected["native"]
        assert classification_matches(outcome.result, expected)
        assert (
            int(outcome.result["matched_frames"])
            + int(outcome.result["mismatched_frames"])
            == int(outcome.result["frames"])
        )
    assert wall_seconds < 3.0
