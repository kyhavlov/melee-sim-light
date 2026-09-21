from pathlib import Path

import pytest

from tools.validation import validate_replay
from tools.validation.suite_io import load_suite

ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "replays/suites/legacy_arithmetic.json"
FIXTURES = ROOT / "replays/validation/legacy_arithmetic"
# An independent corpus replay for the configuration plumbing tests.
CORPUS_REPLAY = ROOT / "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz"


def _require_native(*paths):
    if not all(p.is_file() for p in (*paths, validate_replay.NATIVE, validate_replay.NATIVE_BINARY)):
        pytest.skip("native replay validation artifacts are unavailable")


@pytest.mark.parametrize(
    "name,frames,first,count",
    [("mewtwo_fox_20260702T170056", 5960, 1000, 50),
     ("mewtwo_fox_shfair_only", 8060, 4041, 20),
     ("gamewatch_fox_20260707T142746", 15995, 6573, 62),
     ("gamewatch_fox_20260707T172242", 13196, 1146, 68)],
)
def test_recording_arithmetic_profile_is_strict_and_per_job(name, frames, first, count):
    replay = FIXTURES / f"{name}.slpz"
    _require_native(replay)
    # Alternate on one persistent runner to catch profile leakage between jobs.
    cases = [validate_replay.ReplayCase(replay, name, fnmsubs_profile=profile)
             for profile in ("dolphin-legacy", "retail", None, "dolphin-legacy")]
    outcomes, _ = validate_replay.run_cases(
        validate_replay.load_native(), cases, workers=1, frames=0,
        start_frame=None, timeout=20, backend="native", signed_zero_equal=False, diagnostic=True,
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
        SUITE, characters=frozenset(("mewtwo", "game & watch", "fox")), stages=frozenset((32,)),
    )
    assert len(cases) == 3
    assert all(case.fnmsubs_profile == "dolphin-legacy" for case in cases)


def test_suite_rejects_unknown_arithmetic_profile(tmp_path):
    path = tmp_path / "invalid.json"
    path.write_text('{"name":"invalid","replays":[{"replay":"a.slp",'
                    '"ports":[1,2],"fnmsubs_profile":"guess"}]}')
    with pytest.raises(ValueError, match="invalid fnmsubs_profile"):
        load_suite(path)


def test_explicit_profile_overrides_metadata_on_a_corpus_replay():
    _require_native(CORPUS_REPLAY)
    cases = [validate_replay.ReplayCase(CORPUS_REPLAY, CORPUS_REPLAY.name, fnmsubs_profile=profile)
             for profile in (None, "retail", "dolphin-legacy")]
    outcomes, _ = validate_replay.run_cases(
        validate_replay.load_native(), cases, workers=1, frames=600,
        start_frame=None, timeout=20, backend="native", signed_zero_equal=False, diagnostic=True,
    )
    for case, outcome in zip(cases, outcomes):
        assert outcome.error is None
        assert outcome.result["fnmsubs_profile"] == (case.fnmsubs_profile or "metadata")
        assert outcome.result["frames"] == 600


def test_benchmark_retains_profile_and_separates_cached_tapes(tmp_path):
    from dataclasses import replace
    from peppi_py import _read_slippi
    from tools.validation.prepare_replay_benchmark import _cache_key
    from tools.validation.slpz import replay_path_for_peppi

    _require_native(CORPUS_REPLAY)
    native = validate_replay.load_native()
    tapes = []
    with replay_path_for_peppi(CORPUS_REPLAY) as path:
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
    case = validate_replay.ReplayCase(CORPUS_REPLAY, CORPUS_REPLAY.name)
    assert len({_cache_key(replace(case, fnmsubs_profile=profile))
                for profile in (None, "retail", "dolphin-legacy")}) == 3
