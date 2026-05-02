from __future__ import annotations

from pathlib import Path

from peppi_py import _read_slippi

from tools.slippi.suite_io import load_suite


def test_pokemon_stadium_validation_replays_are_frozen() -> None:
    suite = load_suite("replays/suites/pokemon_stadium_recent.json")
    assert suite.replays
    for replay in suite.replays:
        game = _read_slippi(str(Path(replay.replay)), False)
        assert int(game.start["stage"]) == 3
        assert bool(game.start.get("is_frozen_ps")) is True


def test_aggregate_pokemon_stadium_entries_are_frozen() -> None:
    suite = load_suite("replays/suites/aggregate_recent.json")
    ps_replays = [replay for replay in suite.replays if int(replay.stage_id or -1) == 3]
    assert ps_replays
    for replay in ps_replays:
        game = _read_slippi(str(Path(replay.replay)), False)
        assert int(game.start["stage"]) == 3
        assert bool(game.start.get("is_frozen_ps")) is True
