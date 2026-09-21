from __future__ import annotations

import copy
import json
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest
from peppi_py import _read_slippi

from tools.validation import admission, validate_replay
from tools.validation.slpz import replay_path_for_peppi
from tools.validation.suite_io import load_suite

ROOT = Path(__file__).resolve().parents[1]
VALID = "replays/validation/aggregate_recent/PutridJoyousOryx.slpz"


def read_game(name):
    with replay_path_for_peppi(ROOT / name) as path:
        original = _read_slippi(str(path), False)
        game = SimpleNamespace(frames=original.frames, start=copy.deepcopy(original.start),
                               metadata=dict(original.metadata), end=original.end)
        return game, path.read_bytes()


@pytest.fixture
def properties_only(monkeypatch):
    # Exercise the rule, not the saved exclusion identity for the same fixture.
    monkeypatch.setattr(admission, "evidence", lambda: {"excluded": []})


def test_exact_recording_is_admissible():
    game, raw = read_game(VALID)
    assert admission.capture_issues(game, raw) == []


@pytest.mark.parametrize("name,reason", [
    ("replays/validation/aggregate_recent/HungryImportantSnake.slpz", "vanilla-magnifier"),
    ("replays/validation/marios/64303_master-platinum-f57bf677ef721a6e844c0398.slpz", "missing-recorded-inputs"),
    ("replays/validation/aggregate_recent/Game_20260514T181413.slpz", "incomplete-history"),
])
def test_capture_rules_apply_independently_of_exclusion_identity(properties_only, name, reason):
    game, raw = read_game(name)
    assert reason in admission.capture_issues(game, raw)


def test_zero_chain_samples_are_still_uninitialized(properties_only):
    game, raw = read_game("replays/validation/sheik/PaleMajorEchidna.slpz")
    items = game.frames.field("item").values
    selected = ((items.field("type").to_numpy() == 97) &
                (items.field("state").to_numpy() < 3))
    assert np.any(selected)
    assert np.all(items.field("misc").field("3").to_numpy()[selected] == 0)
    assert "uninitialized-chain" in admission.capture_issues(game, raw)


def test_zero_transform_lcancel_is_still_uninitialized(properties_only):
    game, raw = read_game("replays/validation/ness/2025-11_Game_20251124T141214.slpz")
    found = False
    ports = game.frames.field("ports")
    for port in ports.type.names:
        post = ports.field(port).field("leader").field("post")
        chars = post.field("character").to_pylist()
        initial = next(c for c in chars if c is not None)
        changes = [i for i, c in enumerate(chars) if c in (7, 19) and c != initial]
        if initial in (7, 19) and changes:
            assert post.field("l_cancel")[changes[0]].as_py() == 0
            found = True
    assert found
    assert "uninitialized-transform-lcancel" in admission.capture_issues(game, raw)


def test_legacy_stadium_patch_establishes_frozen_play(properties_only, monkeypatch):
    game, raw = read_game("replays/validation/peach/ScaryFrankPorcupine.slpz")
    assert game.start["stage"] == 3 and game.start["is_frozen_ps"] is False
    assert "unfrozen-stadium" not in admission.capture_issues(game, raw)
    codes = admission.gecko_codes(raw)
    del codes[0xC21D4578]
    monkeypatch.setattr(admission, "gecko_codes", lambda _raw: codes)
    assert "unfrozen-stadium" in admission.capture_issues(game, raw)


def test_unreviewed_offscreen_body_is_rejected(properties_only, monkeypatch):
    game, raw = read_game(VALID)
    codes = admission.gecko_codes(raw)
    patch = bytearray(codes[0xC206A880])
    patch[40] ^= 1
    codes[0xC206A880] = bytes(patch)
    monkeypatch.setattr(admission, "gecko_codes", lambda _raw: codes)
    assert "unreviewed-offscreen-patch" in admission.capture_issues(game, raw)


@pytest.mark.parametrize("key,value", [("is_pal", True), ("item_spawn_frequency", 1),
                                        ("damage_ratio", float("nan")), ("is_raining_bombs", True)])
def test_unsupported_settings_are_rejected(properties_only, key, value):
    game, raw = read_game(VALID)
    game.start[key] = value
    assert "unsupported-settings" in admission.capture_issues(game, raw)


def test_missing_seed_and_frame_gap_are_rejected(properties_only):
    game, raw = read_game(VALID)
    frames = game.frames
    game.frames = frames.slice(1)
    assert "incomplete-history" in admission.capture_issues(game, raw)
    game.frames = frames.filter(np.arange(len(frames)) != 100)
    assert "incomplete-history" in admission.capture_issues(game, raw)


def test_known_unestablished_arithmetic_cannot_be_renamed(tmp_path):
    game, raw = read_game("replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz")
    path = tmp_path / "renamed.slp"
    path.write_bytes(raw)
    with pytest.raises(ValueError, match="unestablished-arithmetic"):
        admission.require_admissible(game, path)


def test_unreviewed_arithmetic_override_is_rejected(properties_only):
    game, raw = read_game(VALID)
    assert "unestablished-arithmetic" in admission.capture_issues(game, raw, fnmsubs_profile="retail")


def test_excluded_captures_are_absent_from_every_suite():
    excluded = {r["replay"] for r in admission.evidence()["excluded"]}
    assert len(excluded) == 133
    for path in (ROOT / "replays/suites").glob("*.json"):
        data = json.loads(path.read_text())
        assert not excluded.intersection(r["replay"] for r in data.get("replays", []))
    aggregate = load_suite(ROOT / "replays/suites/melee_core_aggregate.json")
    pairs = {(c, r.stage_id) for r in aggregate.replays for c in r.characters.values()}
    expected = {(c, stage) for c, stages in admission.evidence()["minimum_coverage"].items()
                for stage in stages}
    assert len(expected) == 148
    assert expected <= pairs
    for entry in admission.evidence()["coverage_replacements"]:
        assert tuple(entry["replaces_pair"]) in pairs
        selected = next(r for r in aggregate.replays if r.replay == entry["replay"])
        game, _ = read_game(entry["replay"])
        # Replay Game Start uses CSS IDs; post rows use internal fighter IDs.
        internal = {5: "Bowser", 21: "Dr. Mario", 24: "Game & Watch", 17: "Luigi",
                    16: "Mewtwo", 23: "Pichu", 18: "Marth", 13: "Samus",
                    22: "Falco", 14: "Yoshi", 1: "Fox", 19: "Zelda"}
        for port, label in selected.characters.items():
            post = game.frames.field("ports").field("P" + port).field("leader").field("post")
            assert internal[post.field("character")[0].as_py()] == label


@pytest.mark.parametrize("arguments", [["--diagnostic"], ["--frames", "10"],
                                       ["--diagnostic-signed-zero-equal"]])
def test_suite_cannot_bypass_strict_admission(monkeypatch, capsys, arguments):
    monkeypatch.setattr("sys.argv", ["validate_replay", "--suite", "unused.json", *arguments])
    with pytest.raises(SystemExit):
        validate_replay.main()
    assert "suite acceptance requires complete strict admitted replays" in capsys.readouterr().err


def test_supported_rule_values_do_not_become_capture_bans(properties_only):
    game, raw = read_game(VALID)
    game.start["damage_ratio"] = 1.2
    for player in game.start["players"]:
        player["stocks"] = 3
        player["handicap"] = 8
    assert "unsupported-settings" not in admission.capture_issues(game, raw)
