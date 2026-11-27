import hashlib
import json
import struct
from pathlib import Path
from zipfile import ZipFile

import pytest
import ubjson
from peppi_py import _read_slippi

from tools.regenerate_replays import regenerate_zip
from tools.validation.slpz import decompress_slpz
from tools.validation.stored import POST_FRAME, _post_rows, _sizes, expected_stored
from tools.validation.validate_replay import NATIVE, NATIVE_BINARY, load_suite


ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture
def falcon_recording():
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native replay validation artifacts are unavailable")
    suite = load_suite(ROOT / "replays/suites/falcon.json")
    case = next(case for case in suite.replays if Path(case.replay).stem == "Game_20260505T114745")
    return decompress_slpz((ROOT / case.replay).read_bytes()), case.stored_sidecar


def test_regenerated_zip_preserves_legacy_rows_and_matches_dolphin(tmp_path, falcon_recording):
    raw, sidecar = falcon_recording
    old_sizes, old_count = _sizes(raw)
    old_rows = _post_rows(raw, old_sizes, old_count)
    original_end = 15 + int.from_bytes(raw[11:15], "big")
    events = raw[15:original_end]
    concatenated = raw[:11] + struct.pack(">I", 2 * len(events)) + events * 2 + raw[original_end:]

    source = tmp_path / "original.zip"
    destination = tmp_path / "regenerated.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("original.slp", raw)
        archive.writestr("truncated.slp", raw[:original_end - 60])
        archive.writestr("concatenated.slp", concatenated)

    result = regenerate_zip(source, destination, workers=2, cores=())
    assert result["outcomes"] == {"regenerated": 1, "parse_failed": 2}
    with ZipFile(destination) as archive:
        assert set(archive.namelist()) == {"original.slp", "regeneration.jsonl"}
        manifest = {row["member"]: row
                    for row in map(json.loads, archive.read("regeneration.jsonl").splitlines())}
        assert manifest["truncated.slp"]["error"]
        assert "unexpected Slippi event 0x35" in manifest["concatenated.slp"]["error"]
        new_raw = archive.read("original.slp")

    new_sizes, new_count = _sizes(new_raw)
    assert new_sizes[POST_FRAME] == 92
    new_rows = _post_rows(new_raw, new_sizes, new_count)
    assert old_rows.keys() == new_rows.keys()
    assert all(new_rows[key].startswith(old) for key, old in old_rows.items())
    assert new_raw[15 + int.from_bytes(new_raw[11:15], "big"):] == raw[original_end:]
    actual = expected_stored(new_raw, None, ROOT)
    dolphin = expected_stored(raw, sidecar, ROOT)
    assert actual == dolphin

    replay = tmp_path / "regenerated.slp"
    replay.write_bytes(new_raw)
    game = _read_slippi(str(replay), False)
    assert game.end is not None
    assert game.frames.field("id").to_pylist() == sorted({frame for frame, _, _ in old_rows})
    with pytest.raises(FileExistsError):
        regenerate_zip(source, destination, workers=1, cores=())


def test_regenerate_finalized_prefix_without_game_end(tmp_path, falcon_recording):
    raw, _ = falcon_recording
    sizes, count = _sizes(raw)
    pos = 17 + 3 * count
    raw_end = 15 + int.from_bytes(raw[11:15], "big")
    while pos < raw_end:
        event = raw[pos]
        end = pos + 1 + sizes[event]
        if event == 0x3C:
            frame, finalized = struct.unpack_from(">ii", raw, pos + 1)
            if frame >= 0 and frame == finalized:
                break
        pos = end
    else:
        pytest.fail("fixture has no finalized frame bookend")

    source = tmp_path / "truncated.zip"
    unfinalized = bytearray(raw[:end])
    struct.pack_into(">i", unfinalized, pos + 5, frame - 1)
    full_file = unfinalized[:11] + struct.pack(">I", end - 15) + unfinalized[15:]
    full_file += (b"U\x08metadata{U\x09lastFramel" + struct.pack(">i", frame)
                  + b"U\x08playedOnSU\x07dolphin}}")
    with ZipFile(source, "w") as archive:
        archive.writestr("finalized.slp", raw[:end])
        archive.writestr("mid-bookend.slp", raw[:end - 1])
        archive.writestr("unfinalized.slp", unfinalized)
        archive.writestr("unfinalized-with-metadata.slp", full_file)

    default = regenerate_zip(source, tmp_path / "default.zip", workers=1, cores=())
    assert default["outcomes"] == {"parse_failed": 3, "unsupported": 1}

    output = tmp_path / "prefixes.zip"
    result = regenerate_zip(source, output, workers=1, cores=(), include_finalized_prefixes=True)
    assert result["outcomes"] == {"regenerated": 2, "parse_failed": 2}
    with ZipFile(output) as archive:
        reports = {r["member"]: r for r in map(json.loads, archive.read("regeneration.jsonl").splitlines())}
        assert reports["finalized.slp"]["complete"] is False
        new_raw = archive.read("finalized.slp")
        assert reports["unfinalized-with-metadata.slp"]["complete"] is False
        assert reports["unfinalized-with-metadata.slp"]["finalized_through"] == frame - 1
        assert reports["unfinalized-with-metadata.slp"]["discarded_tail_frames"] == 1
        trimmed = archive.read("unfinalized-with-metadata.slp")

    replay = tmp_path / "prefix.slp"
    replay.write_bytes(new_raw)
    game = _read_slippi(str(replay), False)
    assert game.end is None
    assert game.frames.field("id").to_pylist() == list(range(-123, frame + 1))
    old_rows = _post_rows(raw, sizes, count)
    new_rows = _post_rows(new_raw, *_sizes(new_raw))
    assert new_rows.keys() == {key for key in old_rows if key[0] <= frame}
    assert all(row.startswith(old_rows[key]) for key, row in new_rows.items())

    replay.write_bytes(trimmed)
    game = _read_slippi(str(replay), False)
    assert game.end is None
    assert game.metadata == {"lastFrame": frame - 1, "playedOn": "dolphin"}
    assert game.frames.field("id").to_pylist() == list(range(-123, frame))
    trimmed_rows = _post_rows(trimmed, *_sizes(trimmed))
    assert trimmed_rows == {key: row for key, row in new_rows.items() if key[0] < frame}
    trimmed_end = 15 + int.from_bytes(trimmed[11:15], "big")
    assert struct.unpack_from(">Bii", trimmed, trimmed_end - 9) == (0x3C, frame - 1, frame - 1)

    recorded_end = 15 + int.from_bytes(new_raw[11:15], "big")
    already_recorded = bytearray(new_raw[:recorded_end])
    struct.pack_into(">i", already_recorded, recorded_end - 4, frame - 1)
    already_recorded.extend(full_file[end:])
    source = tmp_path / "stored.zip"
    output = tmp_path / "stored-prefix.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("stored.slp", already_recorded)
    result = regenerate_zip(source, output, workers=1, cores=(), include_finalized_prefixes=True)
    assert result["outcomes"] == {"already_recorded": 1}
    with ZipFile(output) as archive:
        assert archive.read("stored.slp") == trimmed


def test_regenerate_last_stock_ko_without_game_end(tmp_path, falcon_recording, monkeypatch):
    raw, _ = falcon_recording
    sizes, _ = _sizes(raw)
    end = 15 + int.from_bytes(raw[11:15], "big")
    last_bookend = end - 1 - sizes[0x39]
    assert raw[last_bookend] == 0x39
    assert struct.unpack_from(">Bii", raw, last_bookend - 9) == (0x3C, 10597, 10597)
    missing_end = (raw[:11] + struct.pack(">I", last_bookend - 15)
                   + raw[15:last_bookend] + raw[end:])
    record = {"sha256_slp": hashlib.sha256(missing_end).hexdigest(),
              "reasons": ["incomplete-history"]}
    monkeypatch.setattr("tools.validation.admission.evidence", lambda: {"excluded": [record]})
    source, output = tmp_path / "source.zip", tmp_path / "output.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("missing-end.slp", missing_end)

    assert regenerate_zip(source, output, workers=1, cores=())["outcomes"] == {"regenerated": 1}
    with ZipFile(output) as archive:
        report = json.loads(archive.read("regeneration.jsonl"))
        assert report["game_end"] == "missing" and report["winner"] == "P2"
        updated = archive.read("missing-end.slp")
    replay = tmp_path / "missing-end.slp"
    replay.write_bytes(updated)
    game = _read_slippi(str(replay), False)
    assert game.end is None
    assert game.frames.field("id")[-1].as_py() == 10597
    updated_end = 15 + int.from_bytes(updated[11:15], "big")
    assert updated[updated_end:] == raw[end:]


def test_movement_tolerance_only_accepts_small_movement_differences(tmp_path, falcon_recording):
    raw, _ = falcon_recording
    sizes, count = _sizes(raw)
    small_move = bytearray(raw)
    large_move = bytearray(raw)
    stock_change = bytearray(raw)
    action_change = bytearray(raw)
    pos = 17 + count * 3
    end = 15 + int.from_bytes(raw[11:15], "big")
    while pos < end:
        cmd = raw[pos]
        if cmd == POST_FRAME and struct.unpack_from(">iBB", raw, pos + 1) == (0, 0, 0):
            x = struct.unpack_from(">f", raw, pos + 0x0A)[0]
            struct.pack_into(">f", small_move, pos + 0x0A, x + 0.0001)
            struct.pack_into(">f", large_move, pos + 0x0A, x + 0.0004)
            stock_change[pos + 0x21] -= 1
            action = struct.unpack_from(">H", raw, pos + 8)[0]
            struct.pack_into(">H", action_change, pos + 8, action + 1)
        pos += 1 + sizes[cmd]

    source = tmp_path / "movement.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("small-move.slp", small_move)
        archive.writestr("large-move.slp", large_move)
        archive.writestr("stock-change.slp", stock_change)
        archive.writestr("action-change.slp", action_change)
    strict = regenerate_zip(source, tmp_path / "strict.zip", workers=2, cores=())
    assert strict["outcomes"] == {"diverged": 4}

    destination = tmp_path / "relaxed.zip"
    relaxed = regenerate_zip(source, destination, workers=2, cores=(), relaxed_floats=True)
    assert relaxed["outcomes"] == {"regenerated_with_tolerance": 1, "diverged": 3}
    with ZipFile(destination) as archive:
        assert set(archive.namelist()) == {"small-move.slp", "regeneration.jsonl"}
        manifest = {row["member"]: row
                    for row in map(json.loads, archive.read("regeneration.jsonl").splitlines())}
        assert manifest["small-move.slp"]["tolerated_values"] == 1
        output = archive.read("small-move.slp")
    new_sizes, new_count = _sizes(output)
    assert _post_rows(output, new_sizes, new_count)[0, 0, 0].startswith(
        _post_rows(small_move, sizes, count)[0, 0, 0]
    )


def test_split_fma_regeneration_requires_an_exact_replay(tmp_path):
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native replay validation artifacts are unavailable")
    recording = ROOT / "replays/validation/icies/master-diamond-52318ed052aa47b823e4d641.slpz"
    raw = decompress_slpz(recording.read_bytes())
    end = 15 + int.from_bytes(raw[11:15], "big")
    # The anonymized online recording has an empty metadata object.
    assert raw[end:] == b"U\x08metadata{}}"
    raw = raw[:end] + b"U\x08metadata" + ubjson.dumpb({"playedOn": "network"}) + b"}"
    sizes, count = _sizes(raw)
    action_change = bytearray(raw)
    pos = 17 + count * 3
    while pos < end:
        cmd = raw[pos]
        if cmd == POST_FRAME and struct.unpack_from(">iBB", raw, pos + 1) == (0, 0, 0):
            action = struct.unpack_from(">H", raw, pos + 8)[0]
            struct.pack_into(">H", action_change, pos + 8, action + 1)
        pos += 1 + sizes[cmd]
    assert action_change != raw

    source = tmp_path / "source.zip"
    destination = tmp_path / "regenerated.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("original.slp", raw)
        archive.writestr("action-change.slp", action_change)
    result = regenerate_zip(source, destination, workers=2, cores=())
    assert result["outcomes"] == {"regenerated": 1, "diverged": 1}
    with ZipFile(destination) as archive:
        assert set(archive.namelist()) == {"original.slp", "regeneration.jsonl"}
        manifest = {row["member"]: row
                    for row in map(json.loads, archive.read("regeneration.jsonl").splitlines())}
        assert manifest["original.slp"]["jit_arithmetic"] == "split-fma"
        assert "tolerated_values" not in manifest["original.slp"]


def test_uninitialized_lcancel_only_on_transform_frame(tmp_path):
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native replay validation artifacts are unavailable")
    replay = ROOT / "replays/validation/ness/2025-11_Game_20251124T141214.slpz"
    raw = decompress_slpz(replay.read_bytes())
    sizes, count = _sizes(raw)
    uninitialized = bytearray(raw)
    valid_status = bytearray(raw)
    next_frame = bytearray(raw)
    pos = 17 + count * 3
    end = 15 + int.from_bytes(raw[11:15], "big")
    while pos < end:
        cmd = raw[pos]
        if cmd == POST_FRAME:
            frame, port, follower, character = struct.unpack_from(">iBBB", raw, pos + 1)
            if (frame, port, follower, character) == (5491, 0, 0, 19):
                uninitialized[pos + 0x33] = 80
                valid_status[pos + 0x33] = 1
            elif (frame, port, follower, character) == (5492, 0, 0, 19):
                next_frame[pos + 0x33] = 80
        pos += 1 + sizes[cmd]

    source = tmp_path / "source.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("uninitialized.slp", uninitialized)
        archive.writestr("valid-status.slp", valid_status)
        archive.writestr("next-frame.slp", next_frame)
    strict = regenerate_zip(source, tmp_path / "strict.zip", workers=2, cores=())
    assert strict["outcomes"] == {"diverged": 3}

    destination = tmp_path / "relaxed.zip"
    relaxed = regenerate_zip(source, destination, workers=2, cores=(), relaxed_floats=True)
    assert relaxed["outcomes"] == {"regenerated_with_tolerance": 1, "diverged": 2}
    with ZipFile(destination) as archive:
        output = archive.read("uninitialized.slp")
    new_sizes, new_count = _sizes(output)
    post = _post_rows(output, new_sizes, new_count)[5491, 0, 0]
    assert post[0x33] == 80
