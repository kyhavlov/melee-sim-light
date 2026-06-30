from __future__ import annotations

from collections import Counter
from pathlib import Path

from tools.dolphin.patch_slp_preframe_window import (
    _iter_events,
    _load_ubjson_module,
    _parse_event_payload_sizes,
)
from peppi_py import _read_slippi

from tools.slippi.slpz import replay_path_for_peppi
from tools.slippi.suite_io import load_suite

STADIUM_TRANSFORMATION_EVENT = 0x41


def _read_replay(path: Path):
    with replay_path_for_peppi(path) as peppi_path:
        return _read_slippi(str(peppi_path), False)


def _slippi_event_counts(path: Path) -> Counter[int]:
    ubjson = _load_ubjson_module()
    with replay_path_for_peppi(path) as slp_path:
        with slp_path.open("rb") as f:
            root = ubjson.load(f)
    raw = bytearray(root["raw"])
    sizes = _parse_event_payload_sizes(raw)
    return Counter(command for _, command, _ in _iter_events(raw, sizes))


def test_pokemon_stadium_validation_replays_are_frozen() -> None:
    suite = load_suite("replays/suites/pokemon_stadium_recent.json")
    assert suite.replays
    for replay in suite.replays:
        game = _read_replay(replay.replay)
        assert int(game.start["stage"]) == 3
        assert bool(game.start.get("is_frozen_ps")) is True


def test_aggregate_pokemon_stadium_entries_are_frozen() -> None:
    suite = load_suite("replays/suites/aggregate_recent.json")
    ps_replays = [replay for replay in suite.replays if int(replay.stage_id or -1) == 3]
    assert ps_replays
    for replay in ps_replays:
        game = _read_replay(replay.replay)
        assert int(game.start["stage"]) == 3
        assert bool(game.start.get("is_frozen_ps")) is True


def test_aggregate_derived_selfplay_stadium_fixture_is_frozen_without_transformations() -> None:
    replay = Path("replays/validation/aggregate_recent/Game_20260515T182447_frozenps.slpz")
    game = _read_replay(replay)
    assert int(game.start["stage"]) == 3
    assert bool(game.start.get("is_frozen_ps")) is True
    assert _slippi_event_counts(replay).get(STADIUM_TRANSFORMATION_EVENT, 0) == 0
