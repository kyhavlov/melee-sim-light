from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np


MAGIC = b"MSLMSO01"
VERSION = 10


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


def read_mslmso01_v1(path: Path) -> MotionStateOwners:
    b = path.read_bytes()
    if len(b) < 52:
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
    ) = struct.unpack_from("<IIIIIIIII", b, 16)
    file_bytes = class_bits_off + action_count * 4
    if file_bytes != len(b):
        raise ValueError(f"MSLMSO01 size mismatch in {path}: header-derived {file_bytes} != {len(b)}")

    def arr(off: int, dtype: str, nbytes: int) -> np.ndarray:
        end = off + action_count * nbytes
        if off < 52 or end > len(b):
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
    )


def read_callback_manifest(path: Path) -> dict[int, str]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("magic") != "MSLMSO01" or int(payload.get("version", -1)) != VERSION:
        raise ValueError(f"bad MSLMSO01 callback manifest header: {path}")
    return {int(row["id"]): str(row["symbol"]) for row in payload["symbols"]}
