from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation import validate_replay
from tools.validation.slpz import replay_path_for_peppi

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / (
    "replays/validation/mewtwo_erickfm/"
    "master-master-5de7543e8002c09425f1f2d4.slpz"
)


def test_fox_shield_recording_preserves_diagonal_input():
    with replay_path_for_peppi(REPLAY) as path:
        game = _read_slippi(str(path), False)
    frames = {row["id"]: row for row in game.frames.slice(475, 4).to_pylist()}
    for frame in (354, 355):
        fox = frames[frame]["ports"]["P2"]["leader"]
        assert fox["pre"]["state"] == fox["post"]["state"] == 178
        assert fox["pre"]["cstick"] == {"x": 0.0, "y": 0.0}
        assert fox["pre"]["buttons_physical"] == 64
        assert fox["post"]["ground"] == 3
    stick = frames[355]["ports"]["P2"]["leader"]["pre"]["joystick"]
    assert stick == pytest.approx({"x": 0.7, "y": -0.7})


def test_legacy_ucf_profile_resolves_full_recording():
    if not all(p.is_file() for p in (validate_replay.NATIVE,
                                    validate_replay.NATIVE_BINARY)):
        pytest.skip("native replay validation artifacts are unavailable")
    # UCF 0.8 suppresses held rim spot dodges on solid ground; 0.84 requires
    # a platform. Keep both profiles on one runner to catch settings leakage.
    # Metadata is stripped: network is an explicit diagnostic assumption.
    cases = [validate_replay.ReplayCase(
        REPLAY, REPLAY.name, played_on="network",
        ucf_cardinals_1_0_enabled=False,
        ucf_shield_drop_084_enabled=modern,
    ) for modern in (False, True, False)]
    outcomes, _ = validate_replay.run_cases(
        validate_replay.load_native(), cases, workers=1, frames=0,
        start_frame=None, timeout=20, backend="native", signed_zero_equal=False,
    )
    for case, outcome in zip(cases, outcomes):
        assert outcome.error is None
        result = outcome.result
        assert result["frames"] == 9458
        assert result["signed_zero_equal_count"] == 0
        if case.ucf_shield_drop_084_enabled:
            assert result["pass"] is False
            assert result["exact_prefix_frames"] == 477
            assert result["first_mismatch_frame"] == 355
            assert result["details"][0] == {
                "frame": 355, "field": "action_id[1]",
                "expected": "178", "actual": "235",
            }
        else:
            assert result["pass"] is True
            assert result["mismatch_count"] == 0
