from __future__ import annotations

import struct
from pathlib import Path

import numpy as np


ANIM_MAGIC = b"SSANIM01"
ANIM_VERSION = 4

# SSANIM01 v4 matrix record layout: <12f (3x4)
# Source pointers:
# - tools/extraction/extract_ecb_extents.py (matrix layout + payload walk)
# - tools/extraction/extract_fighter_anims.py (writer: SSANIM01 v4 + TransN tail)
_MAT_BYTES = 12 * 4
_TRANSN_BYTES_PER_FRAME = 3 * 4


def test_ssanim01_v3_header_is_stale_for_current_contract() -> None:
    stale = ANIM_MAGIC + struct.pack("<IHH", 3, 0, 0)
    with np.testing.assert_raises_regex(ValueError, "unsupported version: 3"):
        _read_header(stale)


def _u32_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little", signed=False)


def _read_header(buf: bytes) -> tuple[int, int, list[int]]:
    if len(buf) < 16:
        raise ValueError("SSANIM01: file too small for header")
    if buf[:8] != ANIM_MAGIC:
        raise ValueError(f"SSANIM01: bad magic: {buf[:8]!r}")
    ver = _u32_le(buf, 8)
    if ver != ANIM_VERSION:
        raise ValueError(f"SSANIM01: unsupported version: {ver} (want {ANIM_VERSION})")
    joint_count, anim_count = struct.unpack_from("<HH", buf, 12)
    off = 16
    if len(buf) < off + joint_count:
        raise ValueError("SSANIM01: truncated joint_parts")
    joint_parts = [int(x) for x in buf[off : off + joint_count]]
    return int(joint_count), int(anim_count), joint_parts


def _find_anim_base_offset(*, buf: bytes, joint_count: int, anim_count: int, msid: int) -> tuple[int, int]:
    off = 16 + joint_count
    for _ai in range(anim_count):
        if off + 4 > len(buf):
            raise ValueError("SSANIM01: truncated anim table")
        msid_i, frame_count = struct.unpack_from("<HH", buf, off)
        off += 4
        base = off
        mats_bytes = int(frame_count) * int(joint_count) * _MAT_BYTES
        transn_bytes = int(frame_count) * _TRANSN_BYTES_PER_FRAME
        need = mats_bytes + transn_bytes
        if off + need > len(buf):
            raise ValueError("SSANIM01: truncated anim payload")
        if int(msid_i) == int(msid):
            return int(frame_count), int(base)
        off += need
    raise KeyError(f"SSANIM01: missing msid={msid}")


def _pick_first_nonempty_anim(*, buf: bytes, joint_count: int, anim_count: int) -> tuple[int, int, int]:
    off = 16 + joint_count
    for _ai in range(anim_count):
        if off + 4 > len(buf):
            raise ValueError("SSANIM01: truncated anim table")
        msid_i, frame_count = struct.unpack_from("<HH", buf, off)
        off += 4
        base = off
        mats_bytes = int(frame_count) * int(joint_count) * _MAT_BYTES
        transn_bytes = int(frame_count) * _TRANSN_BYTES_PER_FRAME
        need = mats_bytes + transn_bytes
        if off + need > len(buf):
            raise ValueError("SSANIM01: truncated anim payload")
        if int(frame_count) > 0:
            return int(msid_i), int(frame_count), int(base)
        off += need
    raise AssertionError("SSANIM01: no non-empty animations found")


def _pick_missing_msid(*, buf: bytes, joint_count: int, anim_count: int) -> int:
    off = 16 + joint_count
    seen: set[int] = set()
    max_msid = -1
    for _ai in range(anim_count):
        if off + 4 > len(buf):
            raise ValueError("SSANIM01: truncated anim table")
        msid_i, frame_count = struct.unpack_from("<HH", buf, off)
        off += 4
        seen.add(int(msid_i))
        if int(msid_i) > max_msid:
            max_msid = int(msid_i)
        mats_bytes = int(frame_count) * int(joint_count) * _MAT_BYTES
        transn_bytes = int(frame_count) * _TRANSN_BYTES_PER_FRAME
        need = mats_bytes + transn_bytes
        if off + need > len(buf):
            raise ValueError("SSANIM01: truncated anim payload")
        off += need
    cand = max_msid + 1
    if 0 <= cand <= 0xFFFF and cand not in seen:
        return int(cand)
    for msid in range(0x10000):
        if msid not in seen:
            return int(msid)
    raise AssertionError("SSANIM01: failed to find missing msid (unexpected full coverage)")


def test_anim_pose_matrix_matches_raw_bytes() -> None:
    import msl_binding

    buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(buf)
    part_to_joint_index = {int(p): i for i, p in enumerate(joint_parts)}

    # Derive a stable, data-driven target from the file itself (no artifact-specific constants).
    msid, frame_count, base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    part_id = int(joint_parts[0])
    frame = 0

    joint_index = part_to_joint_index[part_id]
    assert 0 <= frame < frame_count

    off = base + frame * joint_count * _MAT_BYTES + joint_index * _MAT_BYTES
    py_m = np.frombuffer(buf, dtype="<f4", count=12, offset=off)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        c_m = msl_binding.anim_pose_matrix(1, msid, frame, part_id)
    finally:
        msl_binding.destroy(handle)

    assert isinstance(c_m, np.ndarray)
    assert c_m.dtype == np.float32
    assert c_m.shape == (12,)
    assert np.array_equal(c_m.view(np.uint32), py_m.view(np.uint32))


def test_anim_pose_matrix_raises_on_invalid_inputs() -> None:
    import msl_binding

    buf = Path("data/anims/fox.bin").read_bytes()
    joint_count, anim_count, joint_parts = _read_header(buf)
    msid, frame_count, _base = _pick_first_nonempty_anim(buf=buf, joint_count=joint_count, anim_count=anim_count)
    assert frame_count > 0

    missing_msid = _pick_missing_msid(buf=buf, joint_count=joint_count, anim_count=anim_count)
    missing_part_id = 0xFFFF  # not representable in joint_parts (u8), so guaranteed missing

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        with np.testing.assert_raises(ValueError):
            msl_binding.anim_pose_matrix(1, missing_msid, 0, int(joint_parts[0]))
        with np.testing.assert_raises(ValueError):
            msl_binding.anim_pose_matrix(1, msid, 0, missing_part_id)
    finally:
        msl_binding.destroy(handle)
