from __future__ import annotations

from pathlib import Path

from peppi_py import _read_slippi

from tools.validation.slpz import replay_path_for_peppi
from tools.validation.suite_io import load_suite


def _read_replay(path: Path):
    with replay_path_for_peppi(path) as replay:
        return _read_slippi(str(replay), False)


def _assert_frozen_stadium_suite(name: str) -> None:
    suite = load_suite(f"replays/suites/{name}.json")
    replays = [entry for entry in suite.replays if int(entry.stage_id or -1) == 3]
    assert replays
    for entry in replays:
        game = _read_replay(Path(entry.replay))
        assert int(game.start["stage"]) == 3
        assert bool(game.start.get("is_frozen_ps")) is True


def test_stadium_suites_are_frozen() -> None:
    for name in ("pokemon_stadium_recent", "aggregate_recent", "falcon"):
        _assert_frozen_stadium_suite(name)
