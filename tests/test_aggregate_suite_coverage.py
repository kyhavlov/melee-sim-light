from __future__ import annotations

import pytest

from tools.validation.suite_io import load_suite, suite_manifest_paths


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


@pytest.mark.parametrize(
    "component_suite",
    ("aggregate_recent", "puff", "peach", "doubles_recent", "luigi", "marios"),
)
def test_melee_core_aggregate_contains_every_active_replay(
    component_suite: str,
) -> None:
    aggregate = load_suite("replays/suites/melee_core_aggregate.json")
    component = load_suite(f"replays/suites/{component_suite}.json")

    aggregate_paths = {entry.replay for entry in aggregate.replays}
    component_paths = {entry.replay for entry in component.replays}
    assert component_paths <= aggregate_paths


def test_melee_core_aggregate_is_unique_and_composed_from_focused_manifests() -> None:
    aggregate = load_suite("replays/suites/melee_core_aggregate.json")
    paths = [entry.replay for entry in aggregate.replays]
    manifests = suite_manifest_paths("replays/suites/melee_core_aggregate.json")

    assert len(paths) == 234
    assert len(paths) == len(set(paths))
    assert {path.name for path in manifests} == {
        "melee_core_aggregate.json",
        "aggregate_recent.json",
        "puff.json",
        "peach.json",
        "doubles_recent.json",
        "luigi.json",
        "marios.json",
    }
