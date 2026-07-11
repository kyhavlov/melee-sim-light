from __future__ import annotations

import pytest

from tools.eval.profile_validation_rollout_frames import _selected_entries, _timing_summary
from tools.slippi.suite_io import SuiteReplay


def _entry(name: str, *characters: str) -> SuiteReplay:
    return SuiteReplay(
        replay=name,
        ports=(1, 2),
        characters={str(port): character for port, character in enumerate(characters, start=1)},
    )


def test_character_selection_covers_every_supported_character() -> None:
    entries = (
        _entry("spacies", "Fox", "Falco"),
        _entry("marth", "Marth", "Fox"),
        _entry("falcon", "Captain Falcon", "Falco"),
        _entry("transformers", "Sheik", "Zelda"),
    )
    assert _selected_entries(entries, 1) == list(entries)


def test_character_selection_rejects_incomplete_suite() -> None:
    with pytest.raises(ValueError, match="Captain Falcon, Sheik, Zelda"):
        _selected_entries((_entry("spacies", "Fox", "Falco"), _entry("marth", "Marth")), 1)


def test_timing_summary_uses_nearest_rank_percentiles() -> None:
    events = [{"step_ns": value} for value in range(1, 101)]
    summary = _timing_summary(events, top=2)
    assert summary["p50_ns"] == 50
    assert summary["p95_ns"] == 95
    assert summary["p99_ns"] == 99
    assert summary["max_ns"] == 100
    assert [event["step_ns"] for event in summary["top"]] == [100, 99]
