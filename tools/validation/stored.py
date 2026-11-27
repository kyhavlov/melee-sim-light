"""Slippi 3.20 Post-Frame fighter state and regenerated validation sidecars."""

from __future__ import annotations

import argparse
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from tools.validation.slpz import (
    RAW_HEADER,
    _event_sizes,
    _require_zstandard,
    replay_path_for_peppi,
    resolve_replay_path,
)

POST_FRAME = 0x38
STORED_OFFSET = 0x55
STORED_END = 0x5D
HITSTUN_FLAGS = 0x29
VISIBILITY_FLAGS = 0x2A
MISC_AS = 0x2B
MISC_AS_END = 0x2F
KEY = struct.Struct(">iBB")
ROW = struct.Struct(">iBB8s")


@dataclass(frozen=True)
class StoredSidecar:
    path: str
    replay_sha256: str
    playback_build: str
    recording_codeset: str


def _post_rows(raw: bytes, sizes: list[int], event_count: int) -> dict[tuple[int, int, int], bytes]:
    """Take the last event for each frame, physical port, and fighter role."""
    rows = {}
    offset = 17 + 3 * event_count
    end = 15 + int.from_bytes(raw[11:15], "big")
    if not offset <= end <= len(raw):
        raise ValueError("invalid replay stream length")
    while offset < end:
        command = raw[offset]
        size = sizes[command]
        if size == 0 or offset + 1 + size > end:
            raise ValueError("invalid replay event length")
        if command == POST_FRAME:
            key = KEY.unpack_from(raw, offset + 1)
            rows[key] = raw[offset:offset + 1 + size]
        offset += size + 1
    return rows


def _sizes(raw: bytes) -> tuple[list[int], int]:
    if raw[:11] != RAW_HEADER:
        raise ValueError("invalid Slippi replay header")
    return _event_sizes(raw[15:273])


def expected_stored(raw: bytes, sidecar: StoredSidecar | None, root: Path, *,
                    finalized_through: int | None = None) -> bytes:
    sizes, count = _sizes(raw)
    if sizes[POST_FRAME] >= STORED_END - 1:
        if sidecar is not None:
            raise ValueError("replay has recorded stored fighter state and a sidecar")
        rows = {key: row[STORED_OFFSET:STORED_END]
                for key, row in _post_rows(raw, sizes, count).items()}
    elif sidecar is not None:
        if hashlib.sha256(raw).hexdigest() != sidecar.replay_sha256:
            raise ValueError(f"stored-state sidecar replay hash mismatch: {sidecar.path}")
        payload = _require_zstandard().ZstdDecompressor().decompress((root / sidecar.path).read_bytes())
        rows = {}
        for frame, port, follower, value in ROW.iter_unpack(payload):
            key = (frame, port, follower)
            if key in rows:
                raise ValueError(f"duplicate stored-state sidecar row: {key}")
            rows[key] = value
    else:
        return b""
    if finalized_through is not None:
        rows = {key: value for key, value in rows.items() if key[0] <= finalized_through}
    if not rows:
        raise ValueError("stored fighter state has no post-frame rows")
    first = min(rows)[0]
    span = max(rows)[0] - first + 1
    if span > len(rows):
        raise ValueError("stored fighter state has missing frames")
    # One presence byte and eight little-endian fighter records per frame.
    out = bytearray(4 + 65 * span)
    struct.pack_into("<i", out, 0, first)
    for (frame, port, follower), value in rows.items():
        if port >= 4 or follower >= 2:
            raise ValueError("invalid stored fighter state port or role")
        slot = 4 * follower + port
        offset = 4 + 65 * (frame - first)
        out[offset] |= 1 << slot
        start = offset + 1 + 8 * slot
        out[start:start + 8] = value[:4] + value[4:][::-1]
    return bytes(out)


def _matching_post_frame(old: bytes, new: bytes) -> bool:
    if new.startswith(old):
        return True
    # Match native.c's treatment of render visibility and the inactive misc AS union.
    if (old[:VISIBILITY_FLAGS] != new[:VISIBILITY_FLAGS]
            or (old[VISIBILITY_FLAGS] ^ new[VISIBILITY_FLAGS]) & 0x7F):
        return False
    if old[HITSTUN_FLAGS] & 0x02 and old[MISC_AS:MISC_AS_END] != new[MISC_AS:MISC_AS_END]:
        return False
    return old[MISC_AS_END:] == new[MISC_AS_END:STORED_OFFSET]


def write_sidecar(original: bytes, regenerated: bytes, output: Path) -> str:
    old_sizes, old_count = _sizes(original)
    new_sizes, new_count = _sizes(regenerated)
    if old_sizes[POST_FRAME] != STORED_OFFSET - 1 or new_sizes[POST_FRAME] < STORED_END - 1:
        raise ValueError("expected original 84-byte and regenerated 92-byte Post-Frame payloads")
    old = _post_rows(original, old_sizes, old_count)
    new = _post_rows(regenerated, new_sizes, new_count)
    if not old or old.keys() != new.keys():
        raise ValueError("regeneration changed post-frame coverage")
    if any(not _matching_post_frame(row, new[key]) for key, row in old.items()):
        raise ValueError("regeneration changed original post-frame bytes")
    packed = b"".join(ROW.pack(*key, new[key][STORED_OFFSET:STORED_END]) for key in sorted(old))
    output.write_bytes(_require_zstandard().ZstdCompressor().compress(packed))
    return hashlib.sha256(original).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description="Make a stored-state sidecar from a verified regeneration.")
    parser.add_argument("original", type=Path)
    parser.add_argument("regenerated", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with replay_path_for_peppi(resolve_replay_path(args.original)) as original:
        with replay_path_for_peppi(resolve_replay_path(args.regenerated)) as regenerated:
            print(write_sidecar(original.read_bytes(), regenerated.read_bytes(), args.output))


if __name__ == "__main__":
    main()
