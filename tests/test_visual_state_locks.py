"""Visual-only state (agent_docs/VIEWER_TRACK.md): each recording of
replays/suites/visual_state.json, replayed with the viewer track on, passes
strict gameplay validation and writes the track its lock records."""
from __future__ import annotations

import pytest

from tools.validation import visual_state
from tools.validation.validate_replay import NATIVE, NATIVE_BINARY, load_native

CASES = visual_state.suite_cases()
LOCKS = visual_state.load_locks()


def test_every_visual_state_recording_has_a_lock() -> None:
    assert {case.display_path for case in CASES} == set(LOCKS)


@pytest.mark.parametrize("case", CASES, ids=[case.display_path.rsplit("/", 1)[-1] for case in CASES])
def test_viewer_track_matches_its_lock(case) -> None:
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native core validation artifacts are unavailable")
    native = load_native()
    result = visual_state.run_case(native, case)
    assert result["pass"] is True, result["first_mismatch_frame"]
    lock = LOCKS[case.display_path]
    assert result["frames"] == lock["frames"]
    assert visual_state.differences(lock, result["visual_state"]) == []
    assert result["visual_state"]["fingerprint"] == lock["fingerprint"]
    # The track only reads: the same output with it off.
    plain = visual_state.run_case(native, case, visual_track=False)
    assert plain["actual_output_fingerprint"] == result["actual_output_fingerprint"]
