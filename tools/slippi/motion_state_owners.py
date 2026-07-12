from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np


MAGIC = b"MSLMSO01"
VERSION = 26
HEADER_BYTES = 8 + 4 + 2 + 2 + 4 * 13


@dataclass(frozen=True)
class MotionStateOwners:
    submotion_id: np.ndarray
    x4_flags: np.ndarray
    motion_state_word: np.ndarray
    anim_cb_id: np.ndarray
    iasa_cb_id: np.ndarray
    phys_cb_id: np.ndarray
    coll_cb_id: np.ndarray
    cam_cb_id: np.ndarray
    class_bits: np.ndarray
    class2_bits: np.ndarray
    class3_bits: np.ndarray
    fx_special_kind: np.ndarray
    coll_handler_kind: np.ndarray


def read_mslmso01_v1(path: Path) -> MotionStateOwners:
    b = path.read_bytes()
    if len(b) < HEADER_BYTES:
        raise ValueError(f"MSLMSO01 table too small: {path}")
    if b[:8] != MAGIC:
        raise ValueError(f"bad MSLMSO01 magic in {path}: {b[:8]!r}")
    version, action_count, _reserved = struct.unpack_from("<IHH", b, 8)
    if version != VERSION:
        raise ValueError(f"unsupported MSLMSO01 version in {path}: {version}")
    (
        submotion_off,
        x4_flags_off,
        motion_word_off,
        anim_cb_off,
        iasa_cb_off,
        phys_cb_off,
        coll_cb_off,
        cam_cb_off,
        class_bits_off,
        class2_bits_off,
        class3_bits_off,
        fx_special_kind_off,
        coll_handler_kind_off,
    ) = struct.unpack_from("<IIIIIIIIIIIII", b, 16)
    file_bytes = coll_handler_kind_off + action_count
    if file_bytes != len(b):
        raise ValueError(f"MSLMSO01 size mismatch in {path}: header-derived {file_bytes} != {len(b)}")

    def arr(off: int, dtype: str, nbytes: int) -> np.ndarray:
        end = off + action_count * nbytes
        if off < HEADER_BYTES or end > len(b):
            raise ValueError(f"MSLMSO01 bad table offset in {path}: off={off} end={end}")
        return np.frombuffer(b, dtype=dtype, count=action_count, offset=off).copy()

    return MotionStateOwners(
        submotion_id=arr(submotion_off, "<u2", 2),
        x4_flags=arr(x4_flags_off, "<u4", 4),
        motion_state_word=arr(motion_word_off, "<u4", 4),
        anim_cb_id=arr(anim_cb_off, "<u2", 2),
        iasa_cb_id=arr(iasa_cb_off, "<u2", 2),
        phys_cb_id=arr(phys_cb_off, "<u2", 2),
        coll_cb_id=arr(coll_cb_off, "<u2", 2),
        cam_cb_id=arr(cam_cb_off, "<u2", 2),
        class_bits=arr(class_bits_off, "<u4", 4),
        class2_bits=arr(class2_bits_off, "<u4", 4),
        class3_bits=arr(class3_bits_off, "<u4", 4),
        fx_special_kind=arr(fx_special_kind_off, "u1", 1),
        coll_handler_kind=arr(coll_handler_kind_off, "u1", 1),
    )
