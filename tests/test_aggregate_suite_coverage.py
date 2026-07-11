from __future__ import annotations

import pytest

from tools.slippi.suite_io import load_suite


@pytest.mark.parametrize("focused_suite", ("marth", "sheik", "falcon", "zelda"))
def test_aggregate_contains_every_focused_character_replay(focused_suite: str) -> None:
    aggregate = load_suite("replays/suites/aggregate_recent.json")
    focused = load_suite(f"replays/suites/{focused_suite}.json")

    aggregate_paths = {entry.replay for entry in aggregate.replays}
    focused_paths = {entry.replay for entry in focused.replays}
    assert focused_paths <= aggregate_paths


def test_aggregate_replay_paths_are_unique() -> None:
    aggregate = load_suite("replays/suites/aggregate_recent.json")
    paths = [entry.replay for entry in aggregate.replays]
    assert len(paths) == len(set(paths))
