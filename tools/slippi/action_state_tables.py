from __future__ import annotations

import functools
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np


MAGIC = b"MSLACID1"
VERSION = 3
HEADER_BYTES = 32
FT_MOVE_ID_DEFAULT = 1
U16_MAX = 0xFFFF


@dataclass(frozen=True)
class ActionStateTable:
    move_id: np.ndarray
    x4_flags: np.ndarray
    motion_state_word: np.ndarray

    @property
    def x4_flags_low(self) -> np.ndarray:
        return (self.x4_flags & np.uint32(0xFF)).astype(np.uint8, copy=False)

    @property
    def x9_b1(self) -> np.ndarray:
        # Big-endian MotionState bitfield packing in the decomp initializer word:
        # x9_b0 is bit 23 and x9_b1 is bit 22.
        # refs/melee/src/melee/ft/types.h::MotionState
        return ((self.motion_state_word & np.uint32(1 << 22)) != 0).astype(np.uint8, copy=False)


def read_mslacid1_v3(path: str | Path) -> ActionStateTable:
    p = Path(path)
    buf = p.read_bytes()
    if len(buf) < HEADER_BYTES:
        raise ValueError(f"{p}: too small for MSLACID1 v3 header (size={len(buf)})")
    if buf[:8] != MAGIC:
        raise ValueError(f"{p}: bad magic (want MSLACID1)")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != VERSION:
        raise ValueError(
            f"{p}: unsupported MSLACID1 version {int(ver)}; regenerate v{VERSION} tables with: "
            "make bootstrap ISO=/path/to/SSBM.iso"
        )
    (count,) = struct.unpack_from("<H", buf, 12)
    (reserved,) = struct.unpack_from("<H", buf, 14)
    (move_off,) = struct.unpack_from("<I", buf, 16)
    (flags_off,) = struct.unpack_from("<I", buf, 20)
    (motion_word_off,) = struct.unpack_from("<I", buf, 24)
    (file_bytes,) = struct.unpack_from("<I", buf, 28)

    count_i = int(count)
    if int(reserved) != 0:
        raise ValueError(f"{p}: reserved header field must be 0, got {int(reserved)}")
    if int(file_bytes) != len(buf):
        raise ValueError(f"{p}: file_bytes mismatch: header={int(file_bytes)} actual={len(buf)}")

    def check_range(label: str, off: int, elem_size: int) -> int:
        off_i = int(off)
        if off_i < HEADER_BYTES or off_i + count_i * elem_size > len(buf):
            raise ValueError(f"{p}: {label} table out of range (off={off_i} count={count_i})")
        return off_i

    move_i = check_range("move_id", int(move_off), 2)
    flags_i = check_range("x4_flags", int(flags_off), 4)
    word_i = check_range("motion_state_word", int(motion_word_off), 4)

    return ActionStateTable(
        move_id=np.frombuffer(buf, dtype="<u2", offset=move_i, count=count_i),
        x4_flags=np.frombuffer(buf, dtype="<u4", offset=flags_i, count=count_i),
        motion_state_word=np.frombuffer(buf, dtype="<u4", offset=word_i, count=count_i),
    )


@functools.lru_cache(maxsize=8)
def load_action_state_tables(data_dir: str = "data") -> dict[int, ActionStateTable]:
    # One table per registry character (keyed by internal id); preprocessing must cover every
    # character the data tree was built for, not a hardcoded fox/falco pair.
    from tools.extraction.char_registry import CHARS

    base = Path(str(data_dir)) / "attack_id" / "move_id"
    return {
        info.internal_id: read_mslacid1_v3(base / f"{name}.bin") for name, info in CHARS.items()
    }
