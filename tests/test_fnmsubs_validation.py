from pathlib import Path

import pytest

from tools.validation import validate_replay
from tools.validation.suite_io import load_suite

ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "replays/suites/mewtwo_fnmsubs.json"


@pytest.mark.parametrize(
    "name,frames,first,count",
    [("Game_20260702T170056", 5960, 1000, 50),
     ("mewtwo_shfair_only", 8060, 4041, 20)],
)
def test_recording_arithmetic_profile_is_strict_and_per_job(name, frames, first, count):
    replay = ROOT / "replays/validation/mewtwo_fnmsubs" / f"{name}.slpz"
    if not all(p.is_file() for p in (replay, validate_replay.NATIVE,
                                    validate_replay.NATIVE_BINARY)):
        pytest.skip("native replay validation artifacts are unavailable")
    # Alternate on one persistent runner to catch profile leakage between jobs.
    cases = [validate_replay.ReplayCase(replay, name, fnmsubs_profile=profile)
             for profile in ("dolphin-legacy", "retail", None, "dolphin-legacy")]
    outcomes, _ = validate_replay.run_cases(
        validate_replay.load_native(), cases, workers=1, frames=0,
        start_frame=None, timeout=20, backend="native", signed_zero_equal=False,
    )
    for case, outcome in zip(cases, outcomes):
        assert outcome.error is None
        result = outcome.result
        assert result["frames"] == frames
        assert result["signed_zero_equal_count"] == 0
        assert result["fnmsubs_profile"] == (case.fnmsubs_profile or "metadata")
        if case.fnmsubs_profile == "dolphin-legacy":
            assert result["pass"] is True
            assert result["mismatch_count"] == 0
        else:
            assert result["pass"] is False
            assert result["first_mismatch_frame"] == first
            assert result["mismatched_frames"] == count


def test_suite_retains_recording_arithmetic_profile():
    _, cases = validate_replay.load_suite_cases(
        SUITE, characters=frozenset(("mewtwo", "fox")), stages=frozenset((32,)),
    )
    assert len(cases) == 2
    assert all(case.fnmsubs_profile == "dolphin-legacy" for case in cases)


def test_suite_rejects_unknown_arithmetic_profile(tmp_path):
    path = tmp_path / "invalid.json"
    path.write_text('{"name":"invalid","replays":[{"replay":"a.slp",'
                    '"ports":[1,2],"fnmsubs_profile":"guess"}]}')
    with pytest.raises(ValueError, match="invalid fnmsubs_profile"):
        load_suite(path)


def test_benchmark_retains_profile_and_separates_cached_tapes(tmp_path):
    from dataclasses import replace
    from peppi_py import _read_slippi
    from tools.validation.prepare_replay_benchmark import _cache_key
    from tools.validation.slpz import replay_path_for_peppi

    replay = ROOT / "replays/validation/mewtwo_fnmsubs/mewtwo_shfair_only.slpz"
    if not validate_replay.NATIVE.is_file():
        pytest.skip("native validator is unavailable")
    native = validate_replay.load_native()
    tapes = []
    with replay_path_for_peppi(replay) as path:
        game = _read_slippi(str(path), False)
        for profile in ("retail", "dolphin-legacy"):
            output = tmp_path / f"{profile}.mslrpb"
            native.write_benchmark_case(game.frames, game.start, game.metadata,
                                        str(output), fnmsubs_profile=profile)
            tapes.append(output.read_bytes())
        with pytest.raises(ValueError, match="invalid fnmsubs_profile"):
            native.write_benchmark_case(game.frames, game.start, game.metadata,
                                        str(tmp_path / "bad"), fnmsubs_profile="guess")
        with pytest.raises(ValueError, match="invalid fnmsubs_profile"):
            native.validate_replay(game.frames, game.start, game.metadata,
                                   "unused", "unused", "unused", "unused",
                                   fnmsubs_profile="guess")
    # benchmark_wire.h: 24-byte header; wire.h: capability at config byte 25.
    assert len(tapes[0]) == len(tapes[1])
    assert [(i, a, b) for i, (a, b) in enumerate(zip(*tapes)) if a != b] == [(49, 0, 1)]
    case = validate_replay.ReplayCase(replay, replay.name)
    assert len({_cache_key(replace(case, fnmsubs_profile=profile))
                for profile in (None, "retail", "dolphin-legacy")}) == 3
