from pathlib import Path

from peppi_py import _read_slippi

from tools.validation.admission import gecko_codes, stadium_is_frozen
from tools.validation.slpz import replay_path_for_peppi
from tools.validation.suite_io import load_suite


def test_stadium_suites_are_frozen() -> None:
    suite = load_suite("replays/suites/melee_core_aggregate.json")
    stadium = [entry for entry in suite.replays if entry.stage_id == 3]
    assert stadium
    for entry in stadium:
        with replay_path_for_peppi(Path(entry.replay)) as path:
            game = _read_slippi(str(path), False)
            assert game.start["stage"] == 3
            assert stadium_is_frozen(game.start, gecko_codes(path.read_bytes()))
