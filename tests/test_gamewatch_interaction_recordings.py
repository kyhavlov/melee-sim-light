from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation.slpz import replay_path_for_peppi

ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=None)
def recording(name):
    with replay_path_for_peppi(ROOT / "replays/validation/gamewatch" / f"{name}.slpz") as path:
        game = _read_slippi(str(path), False)
    assert game.end["method"] == "Game"
    assert all(p["type"] == "Human" for p in game.start["players"])
    return game


def post(game):
    return game.frames.field("ports").field("P1").field("leader").field("post")


def entries(game):
    states = post(game).field("state").to_pylist()
    return Counter(b for a, b in zip(states, states[1:]) if a != b)


def test_bucket_absorbs_three_lasers_then_releases_oil():
    game = recording("bucket")
    transitions = entries(game)
    assert transitions[376] == 3  # ftGw_MS_SpecialLwCatch
    assert transitions[377] == 1  # ftGw_MS_SpecialLwShoot
    assert max(post(game).field("percent").to_pylist()) == 0
    states = dict(zip(game.frames.field("id").to_pylist(), post(game).field("state").to_pylist()))
    tracks = defaultdict(list)
    for frame, items in zip(game.frames.field("id").to_pylist(), game.frames.field("item").to_pylist()):
        for item in items:
            if item["type"] == 55:
                tracks[item["id"]].append(frame)
    assert len(tracks) == 3
    assert all(states[track[-1] + 1] == 376 for track in tracks.values())
    assert any(item["type"] == 121 for items in game.frames.field("item").to_pylist() for item in items)


@pytest.mark.parametrize("name,kind", [("bucket_control", 55), ("bucket_missile", 95)])
def test_bucket_negative_controls(name, kind):
    game = recording(name)
    assert entries(game)[376] == 0
    assert entries(game)[377] == 0
    assert max(post(game).field("percent").to_pylist()) > 0
    assert any(item["type"] == kind for items in game.frames.field("item").to_pylist() for item in items)
    if name == "bucket_missile":
        assert entries(game)[375] > 0  # Bucket was active, not merely absent.


def test_judge_and_chef_capture_coverage():
    game = recording("judge_chef")
    assert set(range(355, 364)) <= set(entries(game))  # Judge1..9
    trajectories = {item["misc"]["1"] for items in game.frames.field("item").to_pylist()
                    for item in items if item["type"] == 122}
    assert len(trajectories) >= 3
    assert trajectories <= set(range(5))
