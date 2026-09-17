from collections import defaultdict
from functools import lru_cache
from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation.slpz import replay_path_for_peppi

ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=None)
def recording(name):
    path = ROOT / "replays/validation/mewtwo_interactions" / f"{name}.slpz"
    with replay_path_for_peppi(path) as slp:
        game = _read_slippi(str(slp), False)
    assert game.end["method"] == "Game"
    assert all(player["type"] == "Human" for player in game.start["players"])
    return game


def post(game, port):
    return game.frames.field("ports").field(port).field("leader").field("post")


def projectiles(game, item_type):
    tracks = defaultdict(list)
    for frame, items in zip(game.frames.field("id").to_pylist(),
                            game.frames.field("item").to_pylist()):
        for item in items:
            if item["type"] == item_type:
                tracks[item["id"]].append((frame, item))
    return tracks


def reversed_tracks(game, item_type):
    return [track for track in projectiles(game, item_type).values()
            if any(item["velocity"]["x"] < 0 for _, item in track)
            and any(item["velocity"]["x"] > 0 for _, item in track)]


@pytest.mark.parametrize("name,item_type,reversals", [("reflect", 55, 9), ("missile", 95, 4)])
def test_confusion_reverses_projectiles_and_preserves_original_owner(name, item_type, reversals):
    game = recording(name)
    tracks = reversed_tracks(game, item_type)
    assert len(tracks) == reversals
    assert all(item["owner"] == 1 for track in tracks for _, item in track)
    assert max(post(game, "P1").field("percent").to_pylist()) == 0


@pytest.mark.parametrize("name,item_type", [
    ("reflect_control", 55), ("missile_control", 95), ("missile_early", 95),
])
def test_inactive_or_early_confusion_takes_projectile_damage(name, item_type):
    game = recording(name)
    assert projectiles(game, item_type)
    assert not reversed_tracks(game, item_type)
    assert max(post(game, "P1").field("percent").to_pylist()) > 0


def test_ness_absorbs_partial_and_full_shadow_balls():
    game = recording("absorb")
    target = post(game, "P2")
    states = target.field("state").to_pylist()
    percents = target.field("percent").to_pylist()
    stocks = target.field("stocks").to_pylist()
    frames = game.frames.field("id").to_pylist()
    by_frame = dict(zip(frames, states))
    # ftNs_MS_SpecialLwHit = 369: require the absorption response, not just
    # an article disappearing near a fighter or a percent reset on death.
    assert sum(b == 369 and a != 369 for a, b in zip(states, states[1:])) == 2
    assert any(percents[i - 1] == 8 and percents[i] == 0 and stocks[i - 1] == stocks[i]
               for i in range(1, len(percents)))
    absorbed = [track for track in projectiles(game, 112).values()
                if by_frame.get(track[-1][0] + 1) == 369]
    assert sorted(track[-1][1]["state"] for track in absorbed) == [3, 8]
    assert 343 in post(game, "P1").field("state").to_pylist()  # full-charge loop


def test_shadow_balls_damage_ness_without_magnet():
    game = recording("absorb_control")
    assert 369 not in post(game, "P2").field("state").to_pylist()
    assert max(post(game, "P2").field("percent").to_pylist()) > 30
    assert sorted(track[-1][1]["state"] for track in projectiles(game, 112).values()) == [3, 3, 8]


def test_human_teleport_recordings_include_direct_ledge_catches():
    catches = 0
    for path in sorted((ROOT / "replays/validation/mewtwo_teleport").glob("*.slpz")):
        with replay_path_for_peppi(path) as slp:
            game = _read_slippi(str(slp), False)
        states = post(game, "P2").field("state").to_pylist()
        catches += sum(a in range(353, 359) and b == 252 for a, b in zip(states, states[1:]))
    assert catches == 7
