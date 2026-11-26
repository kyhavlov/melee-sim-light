import json
import struct
from pathlib import Path
from zipfile import ZipFile

import pytest
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

    source = tmp_path / "original.zip"
    destination = tmp_path / "regenerated.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("original.slp", raw)
        archive.writestr("truncated.slp", raw[:original_end - 60])

    result = regenerate_zip(source, destination, workers=2, cores=())
    assert result["outcomes"] == {"regenerated": 1, "parse_failed": 1}
    with ZipFile(destination) as archive:
        assert set(archive.namelist()) == {"original.slp", "regeneration.jsonl"}
        manifest = {row["member"]: row
                    for row in map(json.loads, archive.read("regeneration.jsonl").splitlines())}
        assert manifest["truncated.slp"]["error"]
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


def test_movement_tolerance_only_accepts_small_movement_differences(tmp_path, falcon_recording):
    raw, _ = falcon_recording
    sizes, count = _sizes(raw)
    small_move = bytearray(raw)
    large_move = bytearray(raw)
    stock_change = bytearray(raw)
    pos = 17 + count * 3
    end = 15 + int.from_bytes(raw[11:15], "big")
    while pos < end:
        cmd = raw[pos]
        if cmd == POST_FRAME and struct.unpack_from(">iBB", raw, pos + 1) == (0, 0, 0):
            x = struct.unpack_from(">f", raw, pos + 0x0A)[0]
            struct.pack_into(">f", small_move, pos + 0x0A, x + 0.0001)
            struct.pack_into(">f", large_move, pos + 0x0A, x + 0.0004)
            stock_change[pos + 0x21] -= 1
        pos += 1 + sizes[cmd]

    source = tmp_path / "movement.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("small-move.slp", small_move)
        archive.writestr("large-move.slp", large_move)
        archive.writestr("stock-change.slp", stock_change)
    strict = regenerate_zip(source, tmp_path / "strict.zip", workers=2, cores=())
    assert strict["outcomes"] == {"diverged": 3}

    destination = tmp_path / "relaxed.zip"
    relaxed = regenerate_zip(source, destination, workers=2, cores=(), relaxed_floats=True)
    assert relaxed["outcomes"] == {"regenerated_with_tolerance": 1, "diverged": 2}
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
