from __future__ import annotations

from pathlib import Path

import pytest

from tools.melee_core.validate_replay import (
    NATIVE,
    NATIVE_BINARY,
    PPC_BINARY,
    QEMU,
    load_native,
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
    assert float(result["runner_seconds"]) > 0.0
    assert float(result["end_to_end_seconds"]) >= float(result["runner_seconds"])
    assert result["render_visibility_mismatch_count"] == 0
    assert result["first_render_visibility_mismatch_frame"] is None
    assert result["signed_zero_equal_count"] == 0
    assert result["first_mismatch_frame"] is None
    assert result["mismatch_count"] == 0
    assert result["details"] == []
