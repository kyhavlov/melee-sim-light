import hashlib
import struct
from dataclasses import replace

import pytest

from tools.validation.slpz import RAW_HEADER
from tools.validation.stored import ROW, StoredSidecar, expected_stored, write_sidecar


def replay(payload_size, *rows):
    events = bytes([0x35, 4, 0x38, 0, payload_size]) + b"".join(rows)
    return RAW_HEADER + struct.pack(">I", len(events)) + events


def post(frame, port, follower=0, stored=b""):
    return struct.pack(">BiBB", 0x38, frame, port, follower) + bytes(78) + stored


def test_native_stored_rows_use_finalized_port_and_role():
    charge = bytes.fromhex("0601020301020304")
    other = bytes.fromhex("0001000000000000")
    data = replay(92, post(-123, 2, stored=bytes(8)), post(-123, 2, 1, other),
                  post(-123, 2, stored=charge))
    expected = expected_stored(data, None, None)
    assert expected[:5] == struct.pack("<iB", -123, 0x44)
    assert expected[21:29] == bytes.fromhex("0601020304030201")
    assert expected[53:61] == other


def test_sidecar_binds_to_original_replay_and_requires_matching_post_rows(tmp_path):
    original = replay(84, post(-123, 0))
    changed = bytes.fromhex("0300000100000009")
    newer = replay(92, post(-123, 0, stored=changed))
    path = tmp_path / "bucket.post320.zst"
    sidecar = StoredSidecar(path.name, hashlib.sha256(original).hexdigest(), "test", "test")
    assert expected_stored(original, None, tmp_path) == b""
    assert write_sidecar(original, newer, path) == sidecar.replay_sha256
    assert expected_stored(original, sidecar, tmp_path) == expected_stored(newer, None, tmp_path)
    with pytest.raises(ValueError, match="hash mismatch"):
        expected_stored(original, replace(sidecar, replay_sha256="wrong"), tmp_path)
    with pytest.raises(FileNotFoundError):
        expected_stored(original, replace(sidecar, path="missing.zst"), tmp_path)
    with pytest.raises(ValueError, match="changed post-frame coverage"):
        write_sidecar(original, replay(92, post(-123, 2, stored=changed)), path)
    row = bytearray(post(-123, 0, stored=changed))
    row[7] = 1
    with pytest.raises(ValueError, match="changed original post-frame bytes"):
        write_sidecar(original, replay(92, row), path)
    assert ROW.size == 14


def test_sidecar_uses_gameplay_post_frame_bytes(tmp_path):
    original = bytearray(post(-123, 0))
    regenerated = original + bytes.fromhex("0300000100000009")
    regenerated[0x2A] = 0x80
    regenerated[0x2B:0x2F] = b"\x40\x00\x00\x00"
    path = tmp_path / "expected.post320.zst"
    write_sidecar(replay(84, original), replay(92, regenerated), path)

    original[0x29] = regenerated[0x29] = 0x02
    with pytest.raises(ValueError, match="changed original post-frame bytes"):
        write_sidecar(replay(84, original), replay(92, regenerated), path)
    regenerated[0x2B:0x2F] = original[0x2B:0x2F]
    regenerated[0x2A] |= 0x40
    with pytest.raises(ValueError, match="changed original post-frame bytes"):
        write_sidecar(replay(84, original), replay(92, regenerated), path)
