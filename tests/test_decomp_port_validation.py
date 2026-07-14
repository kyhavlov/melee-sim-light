from __future__ import annotations

from pathlib import Path

import pytest

from tools.decomp_port.validate_replay import (
    BINARY,
    NATIVE,
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


@pytest.mark.skipif(
    not all(path.is_file() for path in (STARTER_REPLAY, BINARY, NATIVE, QEMU)),
    reason="decomp validation artifacts or starter replay are unavailable",
)
def test_native_validation_streams_starter_replay_from_arrow() -> None:
    result = validate_one(
        load_native(),
        # Preserve legacy .slp callers when storage has moved to .slpz.
        STARTER_REPLAY.with_suffix(".slp"),
        frames=1,
        start_frame=None,
        timeout=20.0,
    )

    assert result["frames"] == 1
    assert result["available"] == 2723
    assert result["raw_frames"] == 2724
    assert result["seed_frame"] == -123
    assert result["first_ref_frame"] == -122
    assert result["first_mismatch_frame"] == -122
    fields = {detail["field"] for detail in result["details"]}
    assert "action_id[0]" in fields
    assert "frame_pre_random_seed" not in fields
